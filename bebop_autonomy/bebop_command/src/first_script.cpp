#include "ros/ros.h"
#include "std_msgs/String.h"
#include <string>
#include <iostream>
#include <sstream>
#include "Script_publisher.h"

int main(int argc, char **argv)
{
  ros::init(argc, argv, "nice_reader");

  ros::NodeHandle nh;
  script = new Script();

  script -> advertise(nh);

  ros::Rate loop_rate(1);
  loop_rate.sleep();
  // Command is TAKE_OFF LAND FLIP_FORWARD FLIP_BACKWARD FLIP_LEFT FLIP_RIGHT UPDOWN SIDE
  // Make a time function so that you can know the whole function time****
  if (ros::ok())
  {
    ros::spinOnce();

    script -> script_1("TAKE_OFF");
    script -> script_2("TAKE_OFF");
    script -> script_3("TAKE_OFF");
    script -> script_4("TAKE_OFF");
    script -> script_5("TAKE_OFF");
    script -> script_6("TAKE_OFF");
    script -> script_7("TAKE_OFF");
    script -> timer(5);

    script -> script_1("UPDOWN");
    script -> script_2("UPDOWN");
    script -> script_3("UPDOWN");
    script -> script_4("UPDOWN");
    script -> script_5("UPDOWN");
    script -> script_6("UPDOWN");
    script -> script_7("UPDOWN");
    script -> timer(6);

    script -> script_1("SIDE");
    script -> script_2("SIDE");
    script -> script_3("SIDE");
    script -> script_4("SIDE");
    script -> script_5("SIDE");
    script -> script_6("SIDE");
    script -> script_7("SIDE");
    script -> timer(7);

    script -> script_1("LAND");
    script -> script_2("LAND");
    script -> script_3("LAND");
    script -> script_4("LAND");
    script -> script_5("LAND");
    script -> script_6("LAND");
    script -> script_7("LAND");
    script -> timer(9);


  }
  delete script;

  return 0;
}
