#include "Camera.h"
#include "Control.h"
#include "geometry_msgs/PoseStamped.h"
#include "ros/ros.h"

Camera_node *camera_node;

Camera_node::Camera_node() {}

Camera_node::~Camera_node() {}

// Radian을 Degree로 변환
double Rad2Deg(double rad) { return rad * 180 / M_PI; }

// 쿼터니언 각을 오일러 각으로 변환
double toEulerAngle(double x, double y, double z, double w) {
  double siny = +2.0 * (y * w - x * z);
  double cosy = +1.0 - 2.0 * (y * y + z * z);

  double result = Rad2Deg(atan2(siny, cosy));

  if (result >= 0) {
    return result;
  } else {
    return result + 360;
  }
}

// 옵티트랙으로부터 받은 데이터 처리 및 동적 제어부로 전달
void Camera_node::Reading_camera_1(const geometry_msgs::PoseStamped &msg) {
  control->x[0] = -msg.pose.position.x;
  control->y[0] = msg.pose.position.z;
  control->z[0] = msg.pose.position.y;

  // 0~180 => 0~180, 180~360 => -180~0
  control->yaw[0] =
      toEulerAngle(msg.pose.orientation.x, msg.pose.orientation.y,
                   msg.pose.orientation.z, msg.pose.orientation.w);
  if (takeoff_ready[0] == 0)
    takeoff_ready[0] = 1;

  if (control->manner[0])
    control->position_control(0, control->pub_1[VELOCITY]);
}

void Camera_node::Reading_camera_2(const geometry_msgs::PoseStamped &msg) {
  control->x[1] = -msg.pose.position.x;
  control->y[1] = msg.pose.position.z;
  control->z[1] = msg.pose.position.y;
  control->yaw[1] =
      toEulerAngle(msg.pose.orientation.x, msg.pose.orientation.y,
                   msg.pose.orientation.z, msg.pose.orientation.w);

  if (takeoff_ready[1] == 0)
    takeoff_ready[1] = 1;

  if (control->manner[1])
    control->position_control(1, control->pub_2[VELOCITY]);
}

void Camera_node::Reading_camera_3(const geometry_msgs::PoseStamped &msg) {
  control->x[2] = -msg.pose.position.x;
  control->y[2] = msg.pose.position.z;
  control->z[2] = msg.pose.position.y;
  control->yaw[2] =
      toEulerAngle(msg.pose.orientation.x, msg.pose.orientation.y,
                   msg.pose.orientation.z, msg.pose.orientation.w);

  if (takeoff_ready[2] == 0)
    takeoff_ready[2] = 1;

  if (control->manner[2])
    control->position_control(2, control->pub_3[VELOCITY]);
}

void Camera_node::Reading_camera_4(const geometry_msgs::PoseStamped &msg) {
  control->x[3] = -msg.pose.position.x;
  control->y[3] = msg.pose.position.z;
  control->z[3] = msg.pose.position.y;
  control->yaw[3] =
      toEulerAngle(msg.pose.orientation.x, msg.pose.orientation.y,
                   msg.pose.orientation.z, msg.pose.orientation.w);

  if (takeoff_ready[3] == 0)
    takeoff_ready[3] = 1;

  if (control->manner[3])
    control->position_control(3, control->pub_4[VELOCITY]);
}

void Camera_node::Reading_camera_5(const geometry_msgs::PoseStamped &msg) {
  control->x[4] = -msg.pose.position.x;
  control->y[4] = msg.pose.position.z;
  control->z[4] = msg.pose.position.y;
  control->yaw[4] =
      toEulerAngle(msg.pose.orientation.x, msg.pose.orientation.y,
                   msg.pose.orientation.z, msg.pose.orientation.w);

  if (takeoff_ready[4] == 0)
    takeoff_ready[4] = 1;

  if (control->manner[4])
    control->position_control(4, control->pub_5[VELOCITY]);
}

void Camera_node::Reading_camera_6(const geometry_msgs::PoseStamped &msg) {
  control->x[5] = -msg.pose.position.x;
  control->y[5] = msg.pose.position.z;
  control->z[5] = msg.pose.position.y;
  control->yaw[5] =
      toEulerAngle(msg.pose.orientation.x, msg.pose.orientation.y,
                   msg.pose.orientation.z, msg.pose.orientation.w);

  if (takeoff_ready[5] == 0)
    takeoff_ready[5] = 1;

  if (control->manner[5])
    control->position_control(5, control->pub_6[VELOCITY]);
}

void Camera_node::Reading_camera_7(const geometry_msgs::PoseStamped &msg) {
  control->x[6] = -msg.pose.position.x;
  control->y[6] = msg.pose.position.z;
  control->z[6] = msg.pose.position.y;
  control->yaw[6] =
      toEulerAngle(msg.pose.orientation.x, msg.pose.orientation.y,
                   msg.pose.orientation.z, msg.pose.orientation.w);

  if (takeoff_ready[6] == 0)
    takeoff_ready[6] = 1;

  if (control->manner[6])
    control->position_control(6, control->pub_7[VELOCITY]);
}

// 옵티트랙으로부터 데이터 수신 준비
void Camera_node::subscribe(ros::NodeHandle &nh) {
  sub[0] = nh.subscribe("/vrpn_client_node/RigidBody01/pose", 100,
                        &Camera_node::Reading_camera_1, this);
  sub[1] = nh.subscribe("/vrpn_client_node/RigidBody02/pose", 100,
                        &Camera_node::Reading_camera_2, this);
  sub[2] = nh.subscribe("/vrpn_client_node/RigidBody03/pose", 100,
                        &Camera_node::Reading_camera_3, this);
  sub[3] = nh.subscribe("/vrpn_client_node/RigidBody04/pose", 100,
                        &Camera_node::Reading_camera_4, this);
  sub[4] = nh.subscribe("/vrpn_client_node/RigidBody05/pose", 100,
                        &Camera_node::Reading_camera_5, this);
  sub[5] = nh.subscribe("/vrpn_client_node/RigidBody06/pose", 100,
                        &Camera_node::Reading_camera_6, this);
  sub[6] = nh.subscribe("/vrpn_client_node/RigidBody07/pose", 100,
                        &Camera_node::Reading_camera_7, this);
}
