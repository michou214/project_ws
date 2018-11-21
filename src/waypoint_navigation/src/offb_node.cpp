/* There is the original offb_node at the end => L. 260*/



/**
 * @file offb_node.cpp
 * @brief Offboard control example node, written with MAVROS version 0.19.x, PX4 Pro Flight
 * Stack and tested in Gazebo SITL
 */

#include <eigen3/Eigen/Dense>
#include <eigen3/Eigen/Sparse> 
using namespace Eigen;

#include <ros/ros.h>
#include <geometry_msgs/PoseStamped.h>      // Mavros local pose
#include <geometry_msgs/TransformStamped.h> // Mavros local pose
#include <geometry_msgs/PoseArray.h>        // Tag detection
#include <mavros_msgs/CommandBool.h>
#include <mavros_msgs/SetMode.h>
#include <mavros_msgs/State.h>
#include <gazebo_msgs/ModelStates.h>


#include "std_msgs/String.h"
#include <poll.h>
#include <sstream>
#include <vector>

#include "offb_node.h"

// From Aerial robots
#define POS_ACCEPT_RAD 0.2f


// Global variables
int n_AP=0; //To know how many AP are detected
mavros_msgs::State          current_state;
geometry_msgs::PoseStamped  est_local_pos;      // Mavros local pose
gazebo_msgs::ModelStates    true_local_pos;     // Gazebo true local pose
geometry_msgs::PoseArray    APtag_est_pos;      // Tag detection



int main(int argc, char **argv)
{

    int  next_pose = 0;
    bool armed = true;
    bool traj_done = false;

    // Initialize ROS => where we specify the name of our node
    ros::init(argc, argv, "offb_node");
    ros::NodeHandle nh;

    // The node subscribe to Topic "mavros/state", 10 msgs in buffer before deleting
    ros::Subscriber state_sub = nh.subscribe<mavros_msgs::State>
            ("mavros/state", 10, state_cb);

// The node subscribe to Topic "mavros/local_position/pose", 10 msgs in buffer before deleting
ros::Subscriber est_local_pos_sub = nh.subscribe<geometry_msgs::PoseStamped>
("mavros/local_position/pose", 10, est_local_pos_cb);

// The node subscribe to Topic "gazebo/model_states", 10 msgs in buffer before deleting
ros::Subscriber true_local_pos_sub = nh.subscribe<gazebo_msgs::ModelStates>
("gazebo/model_states", 10, true_local_pos_cb);

// The node subscribe to Topic "/tag_detections_pose", 10 msgs in buffer before deleting
ros::Subscriber APtag_est_pos_sub = nh.subscribe<geometry_msgs::PoseArray>
("tag_detections_pose", 10, APtag_est_pos_cb);

    // The node publish the commanded local position        
    ros::Publisher local_pos_pub = nh.advertise<geometry_msgs::PoseStamped>
            ("mavros/setpoint_position/local", 10);

    // This creates a client for the arming status of the drone
    ros::ServiceClient arming_client = nh.serviceClient<mavros_msgs::CommandBool>
            ("mavros/cmd/arming");

    // This creates a client for the mode set on the drone
    ros::ServiceClient set_mode_client = nh.serviceClient<mavros_msgs::SetMode>
            ("mavros/set_mode");

    //the setpoint publishing rate MUST be faster than 2Hz
    ros::Rate rate(20.0);

    // wait for FCU connection (to  established between MAVROS and the autopilot)
    while(ros::ok() && !current_state.connected){
        ros::spinOnce();
        rate.sleep();
    }

    Vector3f pos      (0.0f, 0.0f, 0.0f);
    Vector3f appos    (0.0f, 0.0f, 0.0f);
    Vector3f goal_pos (0.0f, 0.0f, 0.0f);

    // Waypoint navigation list
    Vector3f pos0( 0.0f,  0.0f, 2.0f);
    Vector3f pos1( 2.0f,  2.0f, 2.0f);
    Vector3f pos2( 2.0f, -2.0f, 2.0f);
    Vector3f pos3(-2.0f, -2.0f, 2.0f);
    Vector3f pos4(-2.0f,  2.0f, 2.0f);
    Vector3f pos5( 0.0f,  0.0f, 1.0f);
    Vector3f pos6( 0.0f,  0.0f, 0.5f);
    Vector3f pos7( 0.0f,  0.0f, 0.0f);

    //Vector3f waypoints[8] = {
    //{ 0.0f,  0.0f, 2.0f},
    //{ 2.0f,  2.0f, 2.0f},
    //{ 2.0f, -2.0f, 2.0f},
    //{-2.0f, -2.0f, 2.0f},
    //{-2.0f,  2.0f, 2.0f},
    //{ 0.0f,  0.0f, 1.0f},
    //{ 0.0f,  0.0f, 0.5f},
    //{ 0.0f,  0.0f, 0.0f}};

    // Create msgs_structure of the type mavros_msgs::SetMode
    mavros_msgs::SetMode offb_set_mode;
    // Fix the param 'custom_mode'
    offb_set_mode.request.custom_mode = "OFFBOARD";

    // Create msgs_structure of the type mavros_msgs::CommandBool
    mavros_msgs::CommandBool arm_cmd;
    arm_cmd.request.value = true;

    // Get the time info
    ros::Time last_request = ros::Time::now();


    while(ros::ok()){
        if( current_state.mode != "OFFBOARD" &&
            (ros::Time::now() - last_request > ros::Duration(5.0)))
        {
            if( set_mode_client.call(offb_set_mode) &&
                offb_set_mode.response.mode_sent)
            {
                ROS_INFO("Offboard enabled");
            }
            last_request = ros::Time::now();
        } 
        else 
        {
            if( !current_state.armed &&
                (ros::Time::now() - last_request > ros::Duration(5.0)))
            {
                if( arming_client.call(arm_cmd) &&
                    arm_cmd.response.success)
                {
                    ROS_INFO("Vehicle armed");
                }
                last_request = ros::Time::now();
            }
        }

        // Current location in vector form.
        pos = conversion_to_vect(est_local_pos);
        //ROS_INFO("Pos[%f,%f,%f]", pos(0),pos(1),pos(2));

        // Choo/*se the next waypoint
        switch(next_pose){
            case 0 : goal_pos = pos0; break;
            case 1 : goal_pos = pos1; break;
            case 2 : goal_pos = pos2; break;
            case 3 : goal_pos = pos3; break;
            case 4 : goal_pos = pos4; break;
            case 5 : goal_pos = pos5; break;
            case 6 : goal_pos = pos6; break;
            case 7 : goal_pos = pos7; break;
            default : goal_pos = goal_pos; break;
        }

        if( arming_client.call(arm_cmd) &&  is_goal_reached(goal_pos, pos, POS_ACCEPT_RAD) )
        {
            Vector3f v;
            v(0) = true_local_pos.pose[2].position.x;
            v(1) = true_local_pos.pose[2].position.y;
            v(2) = true_local_pos.pose[2].position.z;
            //ROS_INFO("pos_GOAL=[%f, %f, %f]", v(0), v(1), v(2));
            
            // Current APtag location in vector form.
            //Vector3f appos;
            //appos(0) = APtag_est_pos.poses[2].position.x;
            //appos(1) = APtag_est_pos.poses[2].position.y;
            //appos(2) = APtag_est_pos.poses[2].position.z;
            //ROS_INFO("AP_Pos[%f,%f,%f]", appos(0),appos(1),appos(2));
            
            if( traj_done && next_pose == 0 )
                next_pose = 5;
            else if (traj_done && next_pose>= 5 && next_pose < 7)
                next_pose += 1;
            else if (next_pose == 4)
            {
                //ROS_INFO("Boucle effectuee");
                traj_done = true;
                next_pose = 0; // go to the middle
            } 
            else if (next_pose == 7)
                current_state.armed = false;
            else 
            {
                next_pose += 1;
                
                //Vector3f v;
                //v(0) = true_local_pos.pose[2].position.x;
                //v(1) = true_local_pos.pose[2].position.y;
                //v(2) = true_local_pos.pose[2].position.z;
                //ROS_INFO("pos_iteration=[%f, %f, %f]", v(0), v(1), v(2));
            }    
            
            // We publish the estimated position
            //ROS_INFO("Boucle en cours");    
            local_pos_pub.publish(conversion_to_msg(goal_pos));
        }
        else
        {
            if (!current_state.armed && traj_done){
                arm_cmd.request.value = false;
                // Disarmed drone
                if( !arming_client.call(arm_cmd) &&
                    !arm_cmd.response.success)
                {
                    ROS_INFO("Vehicle disarmed");
                }
            }
            else
                local_pos_pub.publish(conversion_to_msg(goal_pos));
        }
        
        // Needed or your callbacks would never get called
        ros::spinOnce();
        // Sleep for the time remaining to have 10Hz publish rate
        rate.sleep();
    }

    return 0;
}


// ------------------------------------------------------------------------------------------------


// Callback which will save the current state of the autopilot
// Like 'arming', 'disarming', 'takeoff', 'landing', 'Offboard flags'
//mavros_msgs::State current_state;
void state_cb(const mavros_msgs::State::ConstPtr& msg){
    current_state = *msg;
}

// Callback which will save the estimated local position of the autopilot
//geometry_msgs::PoseStamped est_local_pos;
void est_local_pos_cb(const geometry_msgs::PoseStamped::ConstPtr& est_pos){
    est_local_pos = *est_pos;
//    ROS_INFO("Est. local pos=[%f, %f, %f]", est_pos->pose.position.x,
//                                            est_pos->pose.position.y,
//                                            est_pos->pose.position.z);
}

// Callback which will save the estimated local position of the autopilot
//gazebo_msgs::ModelStates true_local_pos;
void true_local_pos_cb(const gazebo_msgs::ModelStates::ConstPtr& true_pos){
    true_local_pos = *true_pos;
//    ROS_INFO("True local pos=[%f, %f, %f]", true_pos->pose[2].position.x,
//                                            true_pos->pose[2].position.y,
//                                            true_pos->pose[2].position.z);
}

// Callback which will save the estimated local position of the Apriltag
//geometry_msgs::PoseArray APtag_est_pos;
void APtag_est_pos_cb(const geometry_msgs::PoseArray::ConstPtr& AP_est_pos){
    //ROS_INFO("CALLBACK AP");
    APtag_est_pos = *AP_est_pos;
    n_AP = AP_est_pos->poses.size();
    //ROS_INFO("n=%d", n_AP);
    //if(n_AP>0){  
    //    ROS_INFO("APtag est pos=[%f, %f, %f]",  AP_est_pos->poses[0].position.x,
    //                                            AP_est_pos->poses[0].position.y,
    //                                            AP_est_pos->poses[0].position.z);
    //}
}


// To check is the drone is at the "right" place
bool is_goal_reached(Vector3f a, Vector3f b, float tol)
{
    if( (a-b).norm() < tol )   
        return true;
    else                            
        return false;
}

// To convert a vector into msg format
geometry_msgs::PoseStamped conversion_to_msg(Vector3f a){
    geometry_msgs::PoseStamped msg;
    msg.pose.position.x = a(0);
    msg.pose.position.y = a(1);
    msg.pose.position.z = a(2);
    return msg;
}

// To convert a msg format in vector
Vector3f conversion_to_vect(geometry_msgs::PoseStamped a){
    Vector3f v;
    v(0) = a.pose.position.x;
    v(1) = a.pose.position.y;
    v(2) = a.pose.position.z;
    return v;
}

