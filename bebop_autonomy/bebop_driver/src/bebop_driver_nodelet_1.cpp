/**
Software License Agreement (BSD)

\file      bebop_driver_nodelet.cpp
\authors   Mani Monajjemi <mmonajje@sfu.ca>
\copyright Copyright (c) 2015, Autonomy Lab (Simon Fraser University), All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that
the following conditions are met:
 * Redistributions of source code must retain the above copyright notice, this list of conditions and the
   following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
   following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Autonomy Lab nor the names of its contributors may be used to endorse or promote
   products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WAR-
RANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, IN-
DIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <ros/ros.h>
#include <pluginlib/class_list_macros.h>
#include <nodelet/nodelet.h>
#include <nav_msgs/Odometry.h>
#include <tf2/LinearMath/Quaternion.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <tf2_ros/transform_broadcaster.h>
#include <sensor_msgs/NavSatFix.h>

#include <boost/bind.hpp>
#include <boost/make_shared.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <cmath>
#include <algorithm>
#include <string>
#include <cstdio>

#include <bebop_driver/bebop_driver_nodelet_1.h>
#include <bebop_driver/BebopArdrone3Config.h>

// AuxThread 구동하기 위한 헤더
#include "bebop_driver/autogenerated/ardrone3_state_callbacks.h"

PLUGINLIB_EXPORT_CLASS(bebop_driver::BebopDriverNodelet_1, nodelet::Nodelet)

namespace bebop_driver
{

namespace util
{
// 출력된 정보들을 터미널에 띄우기 위한 함수
int BebopPrintToROSLogCB(eARSAL_PRINT_LEVEL level, const char *tag, const char *format, va_list va)
{
  const int32_t sz = vsnprintf(bebop_err_str, BEBOP_ERR_STR_SZ, format, va);
  bebop_err_str[std::min(BEBOP_ERR_STR_SZ, sz) - 1] = '\0';

  static const std::string logger_name = std::string(ROSCONSOLE_NAME_PREFIX) + "." +
      ros::this_node::getName() + ".bebopsdk";

  ROS_LOG(util::arsal_level_to_ros[level], logger_name, "[%s] %s", tag, bebop_err_str);
  return 1;
}

}  // namespace util
// Nodelet에 연결이 되었는지 출력하기 위한 함수
BebopDriverNodelet_1::BebopDriverNodelet_1()
 : bebop_ptr_(new bebop_driver::Bebop(util::BebopPrintToROSLogCB))
{
  NODELET_INFO("Nodelet Cstr");
}
// Nodelet 이니시에이팅을 위한 함수
void BebopDriverNodelet_1::onInit()
{
  ros::NodeHandle& nh = getNodeHandle();
  ros::NodeHandle& private_nh = getPrivateNodeHandle();
  util::ResetTwist(camera_twist_);
  {
    boost::unique_lock<boost::mutex> twist_lock(twist_mutex_);
    util::ResetTwist(prev_bebop_twist_);
    util::ResetTwist(prev_camera_twist_);
    prev_twist_stamp_ = ros::Time(0);
  }

  // Nodelet이 이니시에이팅할 입력될 정보들 정의
  // TODO(mani-monaj): Wrap all calls to .param() in a function call to enable logging
  const bool param_reset_settings = private_nh.param("reset_settings", false);
  const bool param_sync_time = private_nh.param("sync_time", false);
  const std::string& param_camera_info_url = private_nh.param<std::string>("camera_info_url", "");
  const std::string& param_bebop_ip = private_nh.param<std::string>("bebop_ip", "192.168.0.12");

  param_camera_frame_id_ = private_nh.param<std::string>("camera_frame_id", "camera_optical");
  param_odom_frame_id_ = private_nh.param<std::string>("odom_frame_id", "odom");
  param_publish_odom_tf_ = private_nh.param<bool>("publish_odom_tf", true);
  param_cmd_vel_timeout_ = private_nh.param<double>("cmd_vel_timeout", 0.2);

  NODELET_INFO("Connecting to Bebop ...");
  try
  {
    bebop_ptr_->Connect(nh, private_nh, param_bebop_ip);

    if (param_reset_settings)
    {
      NODELET_WARN("Resetting all settings ...");
      bebop_ptr_->ResetAllSettings();

      ros::Rate(ros::Duration(3.0)).sleep();
    }

    if (param_sync_time)
    {
      NODELET_WARN("Syncing system and bebop time ...");
      boost::posix_time::ptime system_time = ros::Time::now().toBoost();
      std::string iso_time_str = boost::posix_time::to_iso_extended_string(system_time);
      bebop_ptr_->SetDate(iso_time_str);
      NODELET_INFO_STREAM("Current time: " << iso_time_str);
      ros::Rate(ros::Duration(3.0)).sleep();
    }

    NODELET_INFO("Fetching all settings from the Drone ...");
    bebop_ptr_->RequestAllSettings();
    ros::Rate(ros::Duration(3.0)).sleep();
  }
  catch (const std::runtime_error& e)
  {
    NODELET_FATAL_STREAM("Init failed: " << e.what());
    // TODO(mani-monaj): Retry mechanism
    throw e;
  }

  // 구동 제어부로부터 각 명령어 입력 받을 준비
  cmd_vel_sub_ = nh.subscribe("cmd_vel", 1, &BebopDriverNodelet_1::CmdVelCallback, this);
  takeoff_sub_ = nh.subscribe("takeoff", 1, &BebopDriverNodelet_1::TakeoffCallback, this);
  land_sub_ = nh.subscribe("land", 1, &BebopDriverNodelet_1::LandCallback, this);
  reset_sub_ = nh.subscribe("reset", 1, &BebopDriverNodelet_1::EmergencyCallback, this);
  flattrim_sub_ = nh.subscribe("flattrim", 1, &BebopDriverNodelet_1::FlatTrimCallback, this);
  navigatehome_sub_ = nh.subscribe("autoflight/navigate_home", 1, &BebopDriverNodelet_1::NavigateHomeCallback, this);
  start_autoflight_sub_ = nh.subscribe("autoflight/start", 1, &BebopDriverNodelet_1::StartAutonomousFlightCallback, this);
  pause_autoflight_sub_ = nh.subscribe("autoflight/pause", 1, &BebopDriverNodelet_1::PauseAutonomousFlightCallback, this);
  stop_autoflight_sub_ = nh.subscribe("autoflight/stop", 1, &BebopDriverNodelet_1::StopAutonomousFlightCallback, this);
  animation_sub_ = nh.subscribe("flip", 1, &BebopDriverNodelet_1::FlipAnimationCallback, this);
  // GUI 상으로 드론 상태 정보를 표현할 준비
  wifi_condition = nh.advertise<std_msgs::UInt8>("wifi_condition", 30);
  odom_pub_ = nh.advertise<nav_msgs::Odometry>("odom", 30);
  gps_fix_pub_ = nh.advertise<sensor_msgs::NavSatFix>("fix", 10, true);

  cinfo_manager_ptr_.reset(new camera_info_manager::CameraInfoManager(nh, "bebop_front", param_camera_info_url));

  dynr_serv_ptr_.reset(new dynamic_reconfigure::Server<bebop_driver::BebopArdrone3Config>(private_nh));
  dynamic_reconfigure::Server<bebop_driver::BebopArdrone3Config>::CallbackType cb =
      boost::bind(&bebop_driver::BebopDriverNodelet_1::ParamCallback, this, _1, _2);

  dynr_serv_ptr_->setCallback(cb);

  aux_thread_ptr_ = boost::make_shared<boost::thread>(
        boost::bind(&bebop_driver::BebopDriverNodelet_1::BebopDriverNodelet_1::AuxThread, this));

  NODELET_INFO_STREAM("Nodelet lwp_id: " << util::GetLWPId());
}

// AuxThread 종료
BebopDriverNodelet_1::~BebopDriverNodelet_1()
{
  NODELET_INFO_STREAM("Bebop Nodelet Dstr: " << bebop_ptr_->IsConnected());
  NODELET_INFO_STREAM("Killing Camera Thread ...");
  if (camera_pub_thread_ptr_)
  {
    camera_pub_thread_ptr_->interrupt();
    camera_pub_thread_ptr_->join();
  }
  NODELET_INFO_STREAM("Killing Aux Thread ...");
  if (aux_thread_ptr_)
  {
    aux_thread_ptr_->interrupt();
    aux_thread_ptr_->join();
  }
}

// 드론 속도 입력
void BebopDriverNodelet_1::CmdVelCallback(const geometry_msgs::TwistConstPtr& twist_ptr)
{
  try
  {
    const geometry_msgs::Twist& bebop_twist_ = *twist_ptr;
    bool is_bebop_twist_changed = false;
    {
      boost::unique_lock<boost::mutex> twist_lock(twist_mutex_);
      is_bebop_twist_changed = !util::CompareTwists(bebop_twist_, prev_bebop_twist_);
      prev_twist_stamp_ = ros::Time::now();
      prev_bebop_twist_ = bebop_twist_;
    }

    // TODO: Always apply zero after non-zero values
    if (is_bebop_twist_changed)
    {
      bebop_ptr_->Move(CLAMP(-bebop_twist_.linear.y, -1.0, 1.0),
                       CLAMP(bebop_twist_.linear.x, -1.0, 1.0),
                       CLAMP(bebop_twist_.linear.z, -1.0, 1.0),
                       CLAMP(-bebop_twist_.angular.z, -1.0, 1.0));
    }
  }
  catch (const std::runtime_error& e)
  {
    ROS_ERROR_STREAM(e.what());
  }
}

// 이륙 입력
void BebopDriverNodelet_1::TakeoffCallback(const std_msgs::EmptyConstPtr& empty_ptr)
{
  try
  {
    bebop_ptr_->Takeoff();
  }
  catch (const std::runtime_error& e)
  {
    ROS_ERROR_STREAM(e.what());
  }
}

// 착륙 입력
void BebopDriverNodelet_1::LandCallback(const std_msgs::EmptyConstPtr& empty_ptr)
{
  try
  {
    bebop_ptr_->Land();
  }
  catch (const std::runtime_error& e)
  {
    ROS_ERROR_STREAM(e.what());
  }
}

// 비상착륙 입력
void BebopDriverNodelet_1::EmergencyCallback(const std_msgs::EmptyConstPtr& empty_ptr)
{
  try
  {
    bebop_ptr_->Emergency();
  }
  catch (const std::runtime_error& e)
  {
    ROS_ERROR_STREAM(e.what());
  }
}

// 플랫트림 입력
void BebopDriverNodelet_1::FlatTrimCallback(const std_msgs::EmptyConstPtr &empty_ptr)
{
  try
  {
    // TODO(mani-monaj): Check if landed
    ROS_INFO("Flat Trim");
    bebop_ptr_->FlatTrim();
  }
  catch (const std::runtime_error& e)
  {
    ROS_ERROR_STREAM(e.what());
  }
}

// Return to Home 기능 입력
void BebopDriverNodelet_1::NavigateHomeCallback(const std_msgs::BoolConstPtr &start_stop_ptr)
{
  try
  {
    ROS_INFO("%sing navigate home behavior ...", start_stop_ptr->data ? "Start" : "Stopp");
    bebop_ptr_->NavigateHome(start_stop_ptr->data);
  }
  catch (const std::runtime_error& e)
  {
    ROS_ERROR_STREAM(e.what());
  }
}

// 패트롤 기능 시작 입력
void BebopDriverNodelet_1::StartAutonomousFlightCallback(const std_msgs::StringConstPtr& filepath_ptr)
{
  std::string filepath;
  if (filepath_ptr->data.empty())
  {
    ROS_WARN("No flight plan provided. Using default: 'flightplan.mavlink'");
    filepath = "flightplan.mavlink";
  }
  else
  {
    filepath = filepath_ptr->data;
  }

  try
  {
    ROS_INFO("Starting autonomous flight path: %s", filepath.c_str());
    bebop_ptr_->StartAutonomousFlight(filepath);
  }
  catch (const std::runtime_error& e)
  {
    ROS_ERROR_STREAM(e.what());
  }
}

// 패트롤 기능 정지 입력
void BebopDriverNodelet_1::PauseAutonomousFlightCallback(const std_msgs::EmptyConstPtr& empty_ptr)
{
  try
  {
    ROS_INFO("Pausing autonomous flight");
    bebop_ptr_->PauseAutonomousFlight();
  }
  catch (const std::runtime_error& e)
  {
    ROS_ERROR_STREAM(e.what());
  }
}

// 패트롤 기능 일시정지 입력
void BebopDriverNodelet_1::StopAutonomousFlightCallback(const std_msgs::EmptyConstPtr& empty_ptr)
{
  try
  {
    ROS_INFO("Stopping autonomous flight");
    bebop_ptr_->StopAutonomousFlight();
  }
  catch (const std::runtime_error& e)
  {
    ROS_ERROR_STREAM(e.what());
  }
}

// 플립동작 입력
void BebopDriverNodelet_1::FlipAnimationCallback(const std_msgs::UInt8ConstPtr &animid_ptr)
{
  try
  {
    // TODO(mani-monaj): Check if flying
    ROS_INFO("Performing flip animation %d ...", animid_ptr->data);
    bebop_ptr_->AnimationFlip(animid_ptr->data);
  }
  catch (const std::runtime_error& e)
  {
    ROS_ERROR_STREAM(e.what());
  }
}

// 드론에 업데이트된 정보 입력
void BebopDriverNodelet_1::ParamCallback(BebopArdrone3Config &config, uint32_t level)
{
  NODELET_INFO("Dynamic reconfigure callback with level: %d", level);
  bebop_ptr_->UpdateSettings(config);
}

// 드론에 업데이트할 데이터 정보 입력
void BebopDriverNodelet_1::AuxThread()
{
  NODELET_INFO_STREAM("[AuxThread] thread lwp_id: " << util::GetLWPId());
  // 15Hz 속도로 드론 데이터를 전송한다.
  ros::Rate loop_rate(15.0);
  geometry_msgs::Twist zero_twist;
  util::ResetTwist(zero_twist);

  bebop_msgs::Ardrone3PilotingStateSpeedChanged::ConstPtr speed_esd_ptr;
  bebop_msgs::Ardrone3PilotingStateAttitudeChanged::ConstPtr attitude_ptr;
  bebop_msgs::Ardrone3PilotingStatePositionChanged::ConstPtr gps_state_ptr;

  // 속도
  double beb_roll_rad = 0.0;
  double beb_pitch_rad = 0.0;
  double beb_yaw_rad = 0.0;
  double beb_vx_m = 0.0;
  double beb_vy_m = 0.0;
  double beb_vz_m = 0.0;

  // TF2 데이터
  ros::Time last_odom_time(ros::Time::now());
  geometry_msgs::TransformStamped odom_to_base_tf;
  odom_to_base_tf.header.frame_id = param_odom_frame_id_;
  odom_to_base_tf.child_frame_id = "base_link_1";
  tf2_ros::TransformBroadcaster tf_broad;
  tf2::Vector3 odom_to_base_trans_v3(0.0, 0.0, 0.0);
  tf2::Quaternion odom_to_base_rot_q;

  // 새 정보 갱신
  // TODO(mani-monaj): Wrap this functionality into a class to remove duplicate code
  ros::Time last_speed_time(0);
  ros::Time last_att_time(0);
  ros::Time last_camerastate_time(0);
  ros::Time last_gps_time(0);
  bool new_speed_data = false;
  bool new_attitude_data = false;
  bool new_gps_state = false;

  sensor_msgs::NavSatFix gps_msg;
  gps_msg.header.frame_id = "/gps";
  gps_msg.status.status = sensor_msgs::NavSatStatus::STATUS_NO_FIX;
  gps_msg.status.service = sensor_msgs::NavSatStatus::SERVICE_GPS | sensor_msgs::NavSatStatus::SERVICE_GLONASS;
  gps_msg.position_covariance_type = sensor_msgs::NavSatFix::COVARIANCE_TYPE_UNKNOWN;
  // 드론이 연결됬을 경우에만 데이터를 전송한다.
  while (!boost::this_thread::interruption_requested())
  {
    try
    {
      if (!bebop_ptr_->IsConnected())
      {
        loop_rate.sleep();
        continue;
      }
      {
        const ros::Time t_now = ros::Time::now();

        boost::unique_lock<boost::mutex> twist_lock(twist_mutex_);
        if ( !util::CompareTwists(prev_bebop_twist_, zero_twist) &&
            ((t_now - prev_twist_stamp_).toSec() > param_cmd_vel_timeout_)
           )
        {
          NODELET_WARN("[AuxThread] Input cmd_vel timeout, reseting cmd_vel ...");
          util::ResetTwist(prev_bebop_twist_);
          bebop_ptr_->Move(0.0, 0.0, 0.0, 0.0);
        }
      }

      // 드론 비행 정보
      if (bebop_ptr_->ardrone3_pilotingstate_positionchanged_ptr)
      {
        gps_state_ptr = bebop_ptr_->ardrone3_pilotingstate_positionchanged_ptr->GetDataCstPtr();
        if ((gps_state_ptr->header.stamp - last_gps_time).toSec() > util::eps)
        {
          last_gps_time = gps_state_ptr->header.stamp;
          new_gps_state = true;
        }
      }

      if (bebop_ptr_->ardrone3_pilotingstate_speedchanged_ptr)
      {
        speed_esd_ptr = bebop_ptr_->ardrone3_pilotingstate_speedchanged_ptr->GetDataCstPtr();

        // 새 데이터만 받아드림
        if ((speed_esd_ptr->header.stamp - last_speed_time).toSec() > util::eps)
        {
          last_speed_time = speed_esd_ptr->header.stamp;
          new_speed_data = true;
        }
      }

      if (bebop_ptr_->ardrone3_pilotingstate_attitudechanged_ptr)
      {
        attitude_ptr = bebop_ptr_->ardrone3_pilotingstate_attitudechanged_ptr->GetDataCstPtr();

        // 새 데이터만 받아드림
        if ((attitude_ptr->header.stamp - last_att_time).toSec() > util::eps)
        {
          last_att_time = attitude_ptr->header.stamp;
          beb_roll_rad = attitude_ptr->roll;
          beb_pitch_rad = -attitude_ptr->pitch;
          beb_yaw_rad = -attitude_ptr->yaw;

          odom_to_base_rot_q.setRPY(beb_roll_rad, beb_pitch_rad, beb_yaw_rad);
          new_attitude_data = true;
        }
      }

      const double sync_diff_s = fabs((last_att_time - last_speed_time).toSec());
      // 새 데이터만 받아드림
      if (new_speed_data && new_attitude_data &&
          speed_esd_ptr && attitude_ptr &&
          (sync_diff_s < 0.2))
      {
        ros::Time stamp = std::max(speed_esd_ptr->header.stamp, attitude_ptr->header.stamp);

        const double beb_vx_enu = speed_esd_ptr->speedX;
        const double beb_vy_enu = -speed_esd_ptr->speedY;
        const double beb_vz_enu = -speed_esd_ptr->speedZ;
        beb_vx_m = cos(beb_yaw_rad) * beb_vx_enu + sin(beb_yaw_rad) * beb_vy_enu;
        beb_vy_m = -sin(beb_yaw_rad) * beb_vx_enu + cos(beb_yaw_rad) * beb_vy_enu;
        beb_vz_m = beb_vz_enu;

        const double dt = (ros::Time::now() - last_odom_time).toSec();
        odom_to_base_trans_v3 += tf2::Vector3(beb_vx_enu * dt, beb_vy_enu * dt, beb_vz_enu * dt);

        nav_msgs::OdometryPtr odom_msg_ptr(new nav_msgs::Odometry());
        odom_msg_ptr->header.stamp = stamp;
        odom_msg_ptr->header.frame_id = param_odom_frame_id_;
        odom_msg_ptr->child_frame_id = "base_link_1";
        odom_msg_ptr->twist.twist.linear.x = beb_vx_m;
        odom_msg_ptr->twist.twist.linear.y = beb_vy_m;
        odom_msg_ptr->twist.twist.linear.z = beb_vz_m;

        // TODO(mani-monaj): Optimize this
        odom_msg_ptr->pose.pose.position.x = odom_to_base_trans_v3.x();
        odom_msg_ptr->pose.pose.position.y = odom_to_base_trans_v3.y();
        odom_msg_ptr->pose.pose.position.z = odom_to_base_trans_v3.z();
        tf2::convert(odom_to_base_rot_q, odom_msg_ptr->pose.pose.orientation);
        odom_pub_.publish(odom_msg_ptr);

        temp_number++;
        if ( temp_number > 255 ) temp_number = 0;
        wifi_signal.data = temp_number;
        wifi_condition.publish(wifi_signal);

        if (param_publish_odom_tf_)
        {
          odom_to_base_tf.header.stamp = stamp;
          tf2::convert(tf2::Transform(odom_to_base_rot_q, odom_to_base_trans_v3), odom_to_base_tf.transform);
          tf_broad.sendTransform(odom_to_base_tf);
        }

        last_odom_time = ros::Time::now();
        new_speed_data = false;
        new_attitude_data = false;
      }

      if (new_gps_state && gps_state_ptr)
      {
        // The SDK reports 500, 500, 500 when there is no GPS fix
        const bool is_valid_gps = (fabs(gps_state_ptr->latitude - 500.0) > util::eps) &&
            (fabs(gps_state_ptr->longitude - 500.0) > util::eps) &&
            (fabs(gps_state_ptr->altitude - 500.0) > util::eps);

        gps_msg.header.stamp = gps_state_ptr->header.stamp;
        gps_msg.status.status = is_valid_gps ? static_cast<int8_t>(sensor_msgs::NavSatStatus::STATUS_FIX):
                                               static_cast<int8_t>(sensor_msgs::NavSatStatus::STATUS_NO_FIX);
        gps_msg.latitude = gps_state_ptr->latitude;
        gps_msg.longitude = gps_state_ptr->longitude;
        gps_msg.altitude = gps_state_ptr->altitude;
        gps_fix_pub_.publish(gps_msg);
        new_gps_state = false;
      }

      loop_rate.sleep();
    }
    catch (const std::runtime_error& e)
    {
      NODELET_ERROR_STREAM("[AuxThread] " << e.what());
    }
  }
}

}  // namespace bebop_driver
