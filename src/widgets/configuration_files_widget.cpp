/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2012, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/* Author: Dave Coleman */

// Qt
#include <QVBoxLayout>
#include <QPushButton>
#include <QMessageBox>
#include <QApplication>
#include <QSplitter>
// ROS
#include "configuration_files_widget.h"
#include <srdfdom/model.h> // use their struct datastructures
#include <ros/ros.h>
// Boost
#include <boost/algorithm/string.hpp> // for trimming whitespace from user input
#include <boost/filesystem.hpp>  // for creating folders/files
// Read write files
#include <iostream> // For writing yaml and launch files
#include <fstream>

namespace moveit_setup_assistant
{

// Boost file system
namespace fs = boost::filesystem;

// ******************************************************************************************
// Outer User Interface for MoveIt Configuration Assistant
// ******************************************************************************************
ConfigurationFilesWidget::ConfigurationFilesWidget( QWidget *parent, moveit_setup_assistant::MoveItConfigDataPtr config_data ) :
  SetupScreenWidget( parent ),
  config_data_(config_data),
  has_generated_pkg_(false),
  first_focusGiven_(true)
{
  // Basic widget container
  QVBoxLayout *layout = new QVBoxLayout();

  // Top Header Area ------------------------------------------------

  HeaderWidget *header = new HeaderWidget( "Generate Configuration Files",
                                           "Create or update the configuration files package needed to run your robot with MoveIt. Generated files highlighted orange indicate they were skipped.",
                                           this);
  layout->addWidget( header );

  // Path Widget ----------------------------------------------------

  // Stack Path Dialog
  stack_path_ = new LoadPathWidget("Configuration Package Save Path",
                                   "Specify the desired directory for the MoveIt configuration package to be generated. Overwriting an existing configuration package directory is acceptable. Example: <i>/u/robot/ros/pr2_moveit_config</i>",
                                   true, this); // is directory
  layout->addWidget( stack_path_ );

  // Pass the package path from start screen to configuration files screen
  stack_path_->setPath( config_data_->config_pkg_path_ );


  // Generated Files List -------------------------------------------
  QLabel* generated_list = new QLabel( "Files to be generated:", this );
  layout->addWidget( generated_list );

  QSplitter* splitter = new QSplitter( Qt::Horizontal, this );
  splitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  // List Box
  action_list_ = new QListWidget( this );
  action_list_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
  connect( action_list_, SIGNAL( currentRowChanged(int) ), this, SLOT( changeActionDesc(int) ) );

  // Description
  action_label_ = new QLabel( this );
  action_label_->setFrameShape(QFrame::StyledPanel);
  action_label_->setFrameShadow(QFrame::Raised);
  action_label_->setLineWidth(1);
  action_label_->setMidLineWidth(0);
  action_label_->setWordWrap(true);
  action_label_->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Expanding );
  action_label_->setMinimumWidth( 100 );
  action_label_->setAlignment( Qt::AlignTop );
  action_label_->setOpenExternalLinks(true); // open with web browser

  // Add to splitter
  splitter->addWidget( action_list_ );
  splitter->addWidget( action_label_ );

  // Add Layout
  layout->addWidget( splitter );


  // Progress bar and generate buttons ---------------------------------------------------
  QHBoxLayout *hlayout1 = new QHBoxLayout();

  // Progress Bar
  progress_bar_ = new QProgressBar( this );
  progress_bar_->setMaximum(100);
  progress_bar_->setMinimum(0);
  hlayout1->addWidget(progress_bar_);
  //hlayout1->setContentsMargins( 20, 30, 20, 30 );

  // Generate Package Button
  btn_save_ = new QPushButton("&Generate Package", this);
  //btn_save_->setMinimumWidth(180);
  btn_save_->setMinimumHeight(40);
  connect( btn_save_, SIGNAL( clicked() ), this, SLOT( savePackage() ) );
  hlayout1->addWidget( btn_save_ );

  // Add Layout
  layout->addLayout( hlayout1 );

  // Bottom row --------------------------------------------------

  QHBoxLayout *hlayout3 = new QHBoxLayout();

  // Success label
  success_label_ = new QLabel( this );
  QFont success_label_font( "Arial", 12, QFont::Bold );
  success_label_->setFont( success_label_font );
  success_label_->hide(); // only show once the files have been generated
  success_label_->setText(  "Configuration package generated successfully!" );
  hlayout3->addWidget( success_label_ );
  hlayout3->setAlignment( success_label_, Qt::AlignRight );

  // Exit button
  QPushButton *btn_exit = new QPushButton( "E&xit Setup Assistant", this );
  btn_exit->setMinimumWidth(180);
  connect( btn_exit, SIGNAL( clicked() ), this, SLOT( exitSetupAssistant() ) );
  hlayout3->addWidget( btn_exit );
  hlayout3->setAlignment( btn_exit, Qt::AlignRight );

  layout->addLayout( hlayout3 );

  // Finish Layout --------------------------------------------------
  this->setLayout(layout);

}

// ******************************************************************************************
// Populate the 'Files to be generated' list
// ******************************************************************************************
bool ConfigurationFilesWidget::loadGenFiles()
{
  GenerateFile file; // re-used
  std::string template_path; // re-used
  const std::string robot_name = config_data_->srdf_->robot_name_;

  gen_files_.clear(); // reset vector

  // Get template package location ----------------------------------------------------------------------
  fs::path template_package_path = config_data_->setup_assistant_path_;
  template_package_path /= "templates";
  template_package_path /= "moveit_config_pkg_template";
  config_data_->template_package_path_ = template_package_path.make_preferred().native().c_str();

  if( !fs::is_directory( config_data_->template_package_path_ ) )
  {
    QMessageBox::critical( this, "Error Generating",
                           QString("Unable to find package template directory: ")
                           .append( config_data_->template_package_path_.c_str() ) );
    return false;
  }

  // -------------------------------------------------------------------------------------------------------------------
  // ROS PACKAGE FILES AND FOLDERS ----------------------------------------------------------------------------
  // -------------------------------------------------------------------------------------------------------------------

  // package.xml --------------------------------------------------------------------------------------
  // Note: we call the file package.xml.template so that it isn't automatically indexed by rosprofile
  // in the scenario where we want to disabled the setup_assistant by renaming its root package.xml
  file.file_name_   = "package.xml";
  file.rel_path_    = file.file_name_;
  template_path     = config_data_->appendPaths( config_data_->template_package_path_, "package.xml.template");
  file.description_ = "Defines a ROS package";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // CMakeLists.txt --------------------------------------------------------------------------------------
  file.file_name_   = "CMakeLists.txt";
  file.rel_path_    = file.file_name_;
  template_path     = config_data_->appendPaths( config_data_->template_package_path_, file.file_name_);
  file.description_ = "CMake build system configuration file";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // -------------------------------------------------------------------------------------------------------------------
  // CONIG FILES -------------------------------------------------------------------------------------------------------
  // -------------------------------------------------------------------------------------------------------------------
  std::string config_path = "config";

  // config/ --------------------------------------------------------------------------------------
  file.file_name_   = "config/";
  file.rel_path_    = file.file_name_;
  file.description_ = "Folder containing all MoveIt configuration files for your robot";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::createFolder, this, _1);
  gen_files_.push_back(file);

  // robot.srdf ----------------------------------------------------------------------------------------------
  file.file_name_   = config_data_->urdf_model_->getName() + ".srdf";
  file.rel_path_    = config_data_->appendPaths( config_path, file.file_name_ );
  file.description_ = "SRDF (<a href='http://www.ros.org/wiki/srdf'>Semantic Robot Description Format</a>) is a representation of semantic information about robots. This format is intended to represent information about the robot that is not in the URDF file, but it is useful for a variety of applications. The intention is to include information that has a semantic aspect to it.";
  file.gen_func_    = boost::bind(&SRDFWriter::writeSRDF, config_data_->srdf_, _1);
  gen_files_.push_back(file);
  // special step required so the generated .setup_assistant yaml has this value
  config_data_->srdf_pkg_relative_path_ = file.rel_path_; 

  // ompl_planning.yaml --------------------------------------------------------------------------------------
  file.file_name_   = "ompl_planning.yaml";
  file.rel_path_    = config_data_->appendPaths( config_path, file.file_name_ );
  file.description_ = "Configures the OMPL (<a href='http://ompl.kavrakilab.org/'>Open Motion Planning Library</a>) planning plugin. For every planning group defined in the SRDF, a number of planning configurations are specified (under planner_configs). Additionally, default settings for the state space to plan in for a particular group can be specified, such as the collision checking resolution. Each planning configuration specified for a group must be defined under the planner_configs tag. While defining a planner configuration, the only mandatory parameter is 'type', which is the name of the motion planner to be used. Any other planner-specific parameters can be defined but are optional.";
  file.gen_func_    = boost::bind(&MoveItConfigData::outputOMPLPlanningYAML, config_data_, _1);
  gen_files_.push_back(file);

  // kinematics.yaml  --------------------------------------------------------------------------------------
  file.file_name_   = "kinematics.yaml";
  file.rel_path_    = config_data_->appendPaths( config_path, file.file_name_ );
  file.description_ = "Specifies which kinematic solver plugin to use for each planning group in the SRDF, as well as the kinematic solver search resolution.";
  file.gen_func_    = boost::bind(&MoveItConfigData::outputKinematicsYAML, config_data_, _1);
  gen_files_.push_back(file);

  // joint_limits.yaml --------------------------------------------------------------------------------------
  file.file_name_   = "joint_limits.yaml";
  file.rel_path_    = config_data_->appendPaths( config_path, file.file_name_ );
  file.description_ = "Contains additional information about joints that appear in your planning groups that is not contained in the URDF, as well as allowing you to set maximum and minimum limits for velocity and acceleration than those contained in your URDF. This information is used by our trajectory filtering system to assign reasonable velocities and timing for the trajectory before it is passed to the robots controllers.";
  file.gen_func_    = boost::bind(&MoveItConfigData::outputJointLimitsYAML, config_data_, _1);
  gen_files_.push_back(file);

  // -------------------------------------------------------------------------------------------------------------------
  // LAUNCH FILES ------------------------------------------------------------------------------------------------------
  // -------------------------------------------------------------------------------------------------------------------
  std::string launch_path = "launch";
  const std::string template_launch_path = config_data_->appendPaths( config_data_->template_package_path_, launch_path );

  // launch/ --------------------------------------------------------------------------------------
  file.file_name_   = "launch/";
  file.rel_path_    = file.file_name_;
  file.description_ = "Folder containing all MoveIt launch files for your robot";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::createFolder, this, _1);
  gen_files_.push_back(file);

  // move_group.launch --------------------------------------------------------------------------------------
  file.file_name_   = "move_group.launch";
  file.rel_path_    = config_data_->appendPaths( launch_path, file.file_name_ );
  template_path     = config_data_->appendPaths( template_launch_path, file.file_name_ );
  file.description_ = "Launches the move_group node that provides the MoveGroup action and other parameters <a href='http://moveit.ros.org/move_group.html'>MoveGroup action</a>";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // planning_context.launch --------------------------------------------------------------------------------------
  file.file_name_   = "planning_context.launch";
  file.rel_path_    = config_data_->appendPaths( launch_path, file.file_name_ );
  template_path     = config_data_->appendPaths( template_launch_path, file.file_name_ );
  file.description_ = "Loads settings for the ROS parameter server, required for running MoveIt. This includes the SRDF, joints_limits.yaml file, ompl_planning.yaml file, optionally the URDF, etc";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // moveit_rviz.launch --------------------------------------------------------------------------------------
  file.file_name_   = "moveit_rviz.launch";
  file.rel_path_    = config_data_->appendPaths( launch_path, file.file_name_ );
  template_path     = config_data_->appendPaths( template_launch_path, file.file_name_ );
  file.description_ = "Visualize in Rviz the robot's planning groups running with interactive markers that allow goal states to be set.";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // ompl_planning_pipeline.launch --------------------------------------------------------------------------------------
  file.file_name_   = "ompl_planning_pipeline.launch";
  file.rel_path_    = config_data_->appendPaths( launch_path, file.file_name_ );
  template_path     = config_data_->appendPaths( template_launch_path, file.file_name_ );
  file.description_ = "Intended to be included in other launch files that require the OMPL planning plugin. Defines the proper plugin name on the parameter server and a default selection of planning request adapters.";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // planning_pipeline.launch --------------------------------------------------------------------------------------
  file.file_name_   = "planning_pipeline.launch";
  file.rel_path_    = config_data_->appendPaths( launch_path, file.file_name_ );
  template_path     = config_data_->appendPaths( template_launch_path, file.file_name_ );
  file.description_ = "Helper launch file that can choose between different planning pipelines to be loaded.";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // warehouse_settings.launch --------------------------------------------------------------------------------------
  file.file_name_   = "warehouse_settings.launch";
  file.rel_path_    = config_data_->appendPaths( launch_path, file.file_name_ );
  template_path     = config_data_->appendPaths( template_launch_path, file.file_name_ );
  file.description_ = "Helper launch file that specifies default settings for MongoDB.";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // warehouse.launch --------------------------------------------------------------------------------------
  file.file_name_   = "warehouse.launch";
  file.rel_path_    = config_data_->appendPaths( launch_path, file.file_name_ );
  template_path     = config_data_->appendPaths( template_launch_path, file.file_name_ );
  file.description_ = "Launch file for starting MongoDB.";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // run_benchmark_server_ompl.launch --------------------------------------------------------------------------------------
  file.file_name_   = "run_benchmark_server_ompl.launch";
  file.rel_path_    = config_data_->appendPaths( launch_path, file.file_name_ );
  template_path     = config_data_->appendPaths( template_launch_path, file.file_name_ );
  file.description_ = "Launch file for benchmarking OMPL planners";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // sensor_manager.launch --------------------------------------------------------------------------------------
  file.file_name_   = "sensor_manager.launch";
  file.rel_path_    = config_data_->appendPaths( launch_path, file.file_name_ );
  template_path     = config_data_->appendPaths( template_launch_path, file.file_name_ );
  file.description_ = "Helper launch file that can choose between different sensor managers to be loaded.";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // robot_moveit_controller_manager.launch ------------------------------------------------------------------
  file.file_name_   = robot_name + "_moveit_controller_manager.launch";
  file.rel_path_    = config_data_->appendPaths( launch_path, file.file_name_ );
  template_path     = config_data_->appendPaths( template_launch_path, "moveit_controller_manager.launch" );
  file.description_ = "Placeholder for settings specific to the MoveIt controller manager implemented for you robot.";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // robot_moveit_sensor_manager.launch ------------------------------------------------------------------
  file.file_name_   = robot_name + "_moveit_sensor_manager.launch";
  file.rel_path_    = config_data_->appendPaths( launch_path, file.file_name_ );
  template_path     = config_data_->appendPaths( template_launch_path, "moveit_sensor_manager.launch" );
  file.description_ = "Placeholder for settings specific to the MoveIt sensor manager implemented for you robot.";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // trajectory_execution.launch ------------------------------------------------------------------
  file.file_name_   = "trajectory_execution.launch";
  file.rel_path_    = config_data_->appendPaths( launch_path, file.file_name_ );
  template_path     = config_data_->appendPaths( template_launch_path, file.file_name_ );
  file.description_ = "Loads settings for the ROS parameter server required for executing trajectories using the trajectory_execution_manager::TrajectoryExecutionManager.";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // demo.launch ------------------------------------------------------------------
  file.file_name_   = "demo.launch";
  file.rel_path_    = config_data_->appendPaths( launch_path, file.file_name_ );
  template_path     = config_data_->appendPaths( template_launch_path, file.file_name_ );
  file.description_ = "Run a demo of MoveIt.";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);

  // setup_assistant.launch ------------------------------------------------------------------
  file.file_name_   = "setup_assistant.launch";
  file.rel_path_    = config_data_->appendPaths( launch_path, file.file_name_ );
  template_path     = config_data_->appendPaths( template_launch_path, "edit_configuration_package.launch" );
  file.description_ = "Launch file for easily re-starting the MoveIt Setup Assistant to edit this robot's generated configuration package.";
  file.gen_func_    = boost::bind(&ConfigurationFilesWidget::copyTemplate, this, template_path, _1);
  gen_files_.push_back(file);


  // -------------------------------------------------------------------------------------------------------------------
  // OTHER FILES -------------------------------------------------------------------------------------------------------
  // -------------------------------------------------------------------------------------------------------------------

  // .setup_assistant ------------------------------------------------------------------
  file.file_name_   = ".setup_assistant";
  file.rel_path_    = file.file_name_;
  file.description_ = "MoveIt Setup Assistant hidden settings file. You should not need to edit this file.";
  file.gen_func_    = boost::bind(&MoveItConfigData::outputSetupAssistantFile, config_data_, _1);
  gen_files_.push_back(file);

}

// ******************************************************************************************
// Verify with user if certain screens have not been completed
// ******************************************************************************************
bool ConfigurationFilesWidget::checkDependencies()
{
  QStringList dependencies;

  // Check that at least 1 planning group exists
  if( ! config_data_->srdf_->groups_.size() )
  {
    dependencies << "No robot model planning groups have been created";
  }

  // Check that at least 1 link pair is disabled from collision checking
  if( ! config_data_->srdf_->disabled_collisions_.size() )
  {
    dependencies << "No self-collisions have been disabled";
  }

  // Check that there is at least 1 end effector added
  if( ! config_data_->srdf_->end_effectors_.size() )
  {
    dependencies << "No end effectors have been added";
  }

  // Check that there is at least 1 virtual joint added
  if( ! config_data_->srdf_->virtual_joints_.size() )
  {
    dependencies << "No virtual joints have been added";
  }

  // Display all accumumlated errors:
  if( dependencies.size() )
  {
    // Create a dependency message
    QString dep_message = "Some setup steps have not been completed. None of the steps are required, but here is a reminder of what was not filled in, just in case something was forgotten::<br /><ul>";

    for (int i = 0; i < dependencies.size(); ++i)
    {
      dep_message.append("<li>").append(dependencies.at(i)).append("</li>");
    }
    dep_message.append("</ul><br/>Press Ok to continue generating files.");

    if( QMessageBox::question( this, "Incomplete MoveIt Setup Assistant Steps", dep_message,
                               QMessageBox::Ok | QMessageBox::Cancel)
        == QMessageBox::Cancel )
    {
      return false; // abort
    }
  }

  return true;
}

// ******************************************************************************************
// A function for showing progress and user feedback about what happened
// ******************************************************************************************
void ConfigurationFilesWidget::updateProgress()
{
  action_num_++;

  // Calc percentage
  progress_bar_->setValue( double(action_num_)/gen_files_.size()*100 );

  // allow the progress bar to be shown
  QApplication::processEvents();
}

// ******************************************************************************************
// Display the selected action in the desc box
// ******************************************************************************************
void ConfigurationFilesWidget::changeActionDesc(int id)
{
  // Only allow event if list is not empty
  if( id >= 0 )
  {
    // Show the selected text
    action_label_->setText( action_desc_.at(id) );
  }
}

// ******************************************************************************************
// Called when setup assistant navigation switches to this screen
// ******************************************************************************************
void ConfigurationFilesWidget::focusGiven()
{
  if( !first_focusGiven_ ) // only run this function once
    return;
  else
    first_focusGiven_ = false;

  // Load this list of all files to be generated
  loadGenFiles();

  // Display this list in the GUI
  for (int i = 0; i < gen_files_.size(); ++i)
  {
    GenerateFile* file = &gen_files_[i];

    // Create a formatted row
    QListWidgetItem *item = new QListWidgetItem( QString(file->rel_path_.c_str()), action_list_, 0 );

    //item->setForeground( QBrush(QColor(255, 135, 0)));

    // Add actions to list
    action_list_->addItem( item );
    action_desc_.append( QString( file->description_.c_str() ));
  }

  // Select the first item in the list so that a description is visible
  action_list_->setCurrentRow( 0 );
}

// ******************************************************************************************
// Save configuration click event
// ******************************************************************************************
void ConfigurationFilesWidget::savePackage()
{
  // Feedback
  success_label_->hide();

  // Reset the progress bar counter and GUI stuff
  action_num_ = 0;
  progress_bar_->setValue( 0 );

  if( !generatePackage())
  {
    ROS_ERROR_STREAM("Failed to generate entire configuration package");
    return;
  }

  // Alert user it completed successfully --------------------------------------------------
  progress_bar_->setValue( 100 );
  success_label_->show();
  has_generated_pkg_ = true;
}


// ******************************************************************************************
// Save package using default path
// ******************************************************************************************
bool ConfigurationFilesWidget::generatePackage()
{
  // Get path name
  std::string new_package_path = stack_path_->getPath();

  // Check that a valid stack package name has been given
  if( new_package_path.empty() )
  {
    QMessageBox::warning( this, "Error Generating", "No package path provided. Please choose a directory location to generate the MoveIt configuration files." );
    return false;
  }

  // Check setup assist deps
  if( !checkDependencies() )
    return false; // canceled

  // Check that all groups have components
  if( !noGroupsEmpty() )
    return false; // not ready

  // Trim whitespace from user input
  boost::trim( new_package_path );

  // Get the package name ---------------------------------------------------------------------------------
  new_package_name_ = getPackageName( new_package_path );

  const std::string setup_assistant_file = config_data_->appendPaths( new_package_path, ".setup_assistant" );

  // Make sure old package is correct package type and verify over write
  if( fs::is_directory( new_package_path ) && !fs::is_empty( new_package_path ) )
  {

    // Check if the old package is a setup assistant package. If it is not, quit
    if( ! fs::is_regular_file( setup_assistant_file ) )
    {
      QMessageBox::warning( this, "Incorrect Folder/Package",
                            QString("The chosen package location already exists but was not previously created using this MoveIt Setup Assistant. If this is a mistake, replace the missing file: ")
                            .append( setup_assistant_file.c_str() ) );
      return false;
    }

    // Confirm overwrite
    if( QMessageBox::question( this, "Confirm Package Update",
                               QString("Are you sure you want to overwrite this existing package with updated configurations?<br /><i>")
                               .append( new_package_path.c_str() )
                               .append( "</i>" ),
                               QMessageBox::Ok | QMessageBox::Cancel)
        == QMessageBox::Cancel )
    {
      return false; // abort
    }

  }
  else // this is a new package (but maybe the folder already exists)
  {
    // Create new directory ------------------------------------------------------------------
    try
    {
      fs::create_directory( new_package_path ) && !fs::is_directory( new_package_path );
    }
    catch( ... )
    {
      QMessageBox::critical( this, "Error Generating Files",
                             QString("Unable to create directory ").append( new_package_path.c_str() ) );
      return false;
    }
  }

  // Begin to create files and folders ----------------------------------------------------------------------
  std::string absolute_path;

  for (int i = 0; i < gen_files_.size(); ++i)
  {
    GenerateFile* file = &gen_files_[i];

    // Check if we should skip this file
    if( !file->generate_ )
      continue;

    // Create the absolute path
    absolute_path = config_data_->appendPaths( new_package_path, file->rel_path_ );
    ROS_DEBUG_STREAM("Creating file " << absolute_path );

    // Run the generate function
    if( !file->gen_func_(absolute_path) )
    {
      // Error occured
      QMessageBox::critical( this, "Error Generating File",
                             QString("Failed to generate folder or file: '")
                             .append( file->rel_path_.c_str() ).append("' at location:\n").append( absolute_path.c_str() ));
      return false;
    }
    updateProgress(); // Increment and update GUI
  }

  return true;
}


// ******************************************************************************************
// Quit the program because we are done
// ******************************************************************************************
void ConfigurationFilesWidget::exitSetupAssistant()
{
  if( has_generated_pkg_ || QMessageBox::question( this, "Exit Setup Assistant",
                                                   QString("Are you sure you want to exit the MoveIt Setup Assistant?"),
                                                   QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok )
  {
    QApplication::quit();
  }
}

// ******************************************************************************************
// Get the last folder name in a directory path
// ******************************************************************************************
const std::string ConfigurationFilesWidget::getPackageName( std::string package_path )
{
  // Remove end slash if there is one
  if( !package_path.compare( package_path.size() - 1, 1, "/" ) ) // || package_path[ package_path.size() - 1 ] == "\\" )
  {
    package_path = package_path.substr( 0, package_path.size() - 1 );
  }

  // Get the last directory name
  std::string package_name;
  fs::path fs_package_path = package_path;

  package_name = fs_package_path.filename().c_str();

  // check for empty
  if( package_name.empty() )
    package_name = "unknown";

  return package_name;
}

// ******************************************************************************************
// Check that no group is empty (without links/joints/etc)
// ******************************************************************************************
bool ConfigurationFilesWidget::noGroupsEmpty()
{
  // Loop through all groups
  for( std::vector<srdf::Model::Group>::const_iterator group_it = config_data_->srdf_->groups_.begin();
       group_it != config_data_->srdf_->groups_.end();  ++group_it )
  {
    // Whenever 1 of the 4 component types are found, stop checking this group
    if( group_it->joints_.size() )
      continue;
    if( group_it->links_.size() )
      continue;
    if( group_it->chains_.size() )
      continue;
    if( group_it->subgroups_.size() )
      continue;

    // This group has no contents, bad
    QMessageBox::warning( this, "Empty Group",
                          QString("The planning group '").append( group_it->name_.c_str() )
                          .append("' is empty and has no subcomponents associated with it (joints/links/chains/subgroups). You must edit or remove this planning group before this configuration package can be saved.") );
    return false;
  }

  return true; // good
}

// ******************************************************************************************
// Copy a template from location <template_path> to location <output_path> and replace package name
// ******************************************************************************************
bool ConfigurationFilesWidget::copyTemplate( const std::string& template_path, const std::string& output_path )
{
  // Error check file
  if( ! fs::is_regular_file( template_path ) )
  {
    ROS_ERROR_STREAM( "Unable to find template file " << template_path );
    return false;
  }

  // Load file
  std::ifstream template_stream( template_path.c_str() );
  if( !template_stream.good() ) // File not found
  {
    ROS_ERROR_STREAM( "Unable to load file " << template_path );
    return false;
  }

  // Load the file to a string using an efficient memory allocation technique
  std::string template_string;
  template_stream.seekg(0, std::ios::end);
  template_string.reserve(template_stream.tellg());
  template_stream.seekg(0, std::ios::beg);
  template_string.assign( (std::istreambuf_iterator<char>(template_stream)), std::istreambuf_iterator<char>() );
  template_stream.close();

  // Replace keywords in string ------------------------------------------------------------
  boost::replace_all( template_string, "[GENERATED_PACKAGE_NAME]", new_package_name_ );

  if (config_data_->urdf_pkg_name_.empty())
  {
    boost::replace_all( template_string, "[URDF_LOCATION]", config_data_->urdf_path_ );
  }
  else
  {
    boost::replace_all( template_string, "[URDF_LOCATION]", "$(find " + config_data_->urdf_pkg_name_ + ")/" + config_data_->urdf_pkg_relative_path_);
  }

  boost::replace_all( template_string, "[ROBOT_NAME]", config_data_->srdf_->robot_name_ );

  std::stringstream vjb;
  for (std::size_t i = 0 ; i < config_data_->srdf_->virtual_joints_.size(); ++i)
  {
    const srdf::Model::VirtualJoint &vj = config_data_->srdf_->virtual_joints_[i];
    if (vj.type_ != "fixed")
      vjb << "  <node pkg=\"tf\" type=\"static_transform_publisher\" name=\"virtual_joint_broadcaster_" << i << "\" args=\"0 0 0 0 0 0 " << vj.parent_frame_ << " " << vj.child_link_ << " 100\" />" << std::endl;
  }

  boost::replace_all ( template_string, "[VIRTUAL_JOINT_BROADCASTER]", vjb.str());

  // Save string to new location -----------------------------------------------------------
  std::ofstream output_stream( output_path.c_str(), std::ios_base::trunc );
  if( !output_stream.good() )
  {
    ROS_ERROR_STREAM( "Unable to open file for writing " << output_path );
    return false;
  }

  output_stream << template_string.c_str();
  output_stream.close();

  return true; // file created successfully
}

// ******************************************************************************************
// Create a folder
// ******************************************************************************************
bool ConfigurationFilesWidget::createFolder(const std::string& output_path)
{
  if( !fs::is_directory( output_path ) )
  {
    if ( !fs::create_directory( output_path ) )
    {
      QMessageBox::critical( this, "Error Generating Files",
                             QString("Unable to create directory ").append( output_path.c_str() ) );
      return false;
    }
  }
  return true;
}


} // namespace

