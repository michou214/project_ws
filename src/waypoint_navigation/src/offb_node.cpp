/* There is the original offb_node at the end => L. 260*/



/**
 * @file offb_node.cpp
 * @brief Offboard control example node, written with MAVROS version 0.19.x, PX4 Pro Flight
 * Stack and tested in Gazebo SITL
 */

#include <eigen3/Eigen/Dense>
#include <eigen3/Eigen/Sparse> 
using namespace Eigen;
//namespace apriltags_ros;

#include <ros/ros.h>
#include <geometry_msgs/PoseStamped.h>      // Mavros local pose
#include <geometry_msgs/TransformStamped.h> // Mavros local pose
#include <geometry_msgs/PoseArray.h>        // Tag detection
#include <mavros_msgs/CommandBool.h>
#include <mavros_msgs/SetMode.h>
#include <mavros_msgs/State.h>
#include <gazebo_msgs/ModelStates.h>
#include <apriltags_ros/AprilTagDetectionArray.h>


#include "std_msgs/String.h"
#include <poll.h>
#include <sstream>
#include <vector>
#include <cmath>

#include "offb_node.h"



// Parameters to modify
// --- Apriltag parameters
#define DESIRED_ID 2
// --- Waypoint generation parameters
#define NB_CYCLE 3
#define W 1 // Spacing in the trajectory generation, see drawing
#define L 1 // Spacing in the trajectory generation, see drawing
#define ALPHA 20 // Every ALPHA[°] you take the point on the Archimed spiral
#define SQUARE 1
#define RECT_HORI 2
#define RECT_VERT 3
#define SPIRAL 4  // Parameters for the Archimed spiral (a,b) are in WP_generation()
#define SEARCH_SHAPE SQUARE // Modify to choose the type of search 
#define HEIGHT 1.5f

// No modification are necessary but you can adjuste as you want
#define TOL 0.1f
#define POS_ACCEPT_RAD 0.2f // From Aerial robots
#define AP_SIZE 0.2f
#define AP_POS_ACCEPT 0.1f
#define AP_POS_ACCEPT_X 0.1f
#define AP_POS_ACCEPT_Y 0.1f
#define HEIGHT 1.5f
#define OFFSET_CAM_X 0
#define OFFSET_CAM_Y 0
#define SP_SIZE_WIDTH 0.8f  // SP=Solar Panel of size 1.6x0.8[m]
#define SP_SIZE_LENGTH 1.6f // SP=Solar Panel of size 1.6x0.8[m]
#define SP_SIZE_HEIGHT 0.8f // SP=Solar Panel of size 1.6x0.8[m]


// Global variables
int idx  = 0; // index to select a point in the list of points 
int n_AP = 0; //To know how many AP are detected
int AP_id=10;
int n_model=0;

bool skip  = false;
bool armed = true;
bool traj_done = false;
bool AP_detected  = false;
bool AP_centered  = false;
bool AP_verified  = false;
bool takeoff_done = false;
bool landing_done = false;
bool landing_in_progress = false;



mavros_msgs::State          current_state;
geometry_msgs::PoseStamped  est_local_pos;      // Mavros local pose
gazebo_msgs::ModelStates    true_local_pos;     // Gazebo true local pose
//geometry_msgs::PoseArray    APtag_est_pos;      // Tag detection
apriltags_ros::AprilTagDetectionArray APtag_est_pos;// Tag detection


int main(int argc, char **argv)
{
    int size_wp = 0;

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
//ros::Subscriber APtag_est_pos_sub = nh.subscribe<geometry_msgs::PoseArray>
//("tag_detections_pose", 10, APtag_est_pos_cb);
// The node subscribe to Topic "/tag_detections_pose", 10 msgs in buffer before deleting
ros::Subscriber APtag_est_pos_sub = nh.subscribe<apriltags_ros::AprilTagDetectionArray>
("tag_detections", 10, APtag_est_pos_cb);

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

    // Pos variable to be used
    Vector3f pos        (0.0f, 0.0f, 0.0f);
    Vector3f AP_pos     (0.0f, 0.0f, 0.0f);
    Vector3f AP_pos_save(0.0f, 0.0f, 0.0f);
    Vector3f goal_pos   (5.0f, 5.0f, 5.0f); 
    // Pos variable to be defined
    Vector3f takeoff( 0.0f,  0.0f, HEIGHT);
    
    // -------------------------
    // Waypoint generation
    // -------------------------
    if(SEARCH_SHAPE == SQUARE || SEARCH_SHAPE == RECT_HORI || SEARCH_SHAPE == RECT_VERT)
        size_wp = NB_CYCLE*4+1; // 4 points per cycle + the starting point
    if(SEARCH_SHAPE == SPIRAL)
        size_wp = NB_CYCLE*(360/ALPHA)+1; // 360/alpha points per cycle + starting point

    Vector3f waypoints[size_wp];
    WP_generation(takeoff, NB_CYCLE, W, L, ALPHA, waypoints, size_wp, SEARCH_SHAPE);
    for(int p=0; p<size_wp; p++)
        ROS_INFO("WP[%d] =[%f, %f, %f]", p, waypoints[p](0),waypoints[p](1),waypoints[p](2));

    // Verify this
    /*
    Vector3f pos0( 1.9f,  1.9f, HEIGHT);
    Vector3f pos1( 1.9f, -1.9f, HEIGHT);
    Vector3f pos2(-1.9f, -1.9f, HEIGHT);
    Vector3f pos3(-1.9f,  1.9f, HEIGHT);
    Vector3f waypoints[5] = {pos00, pos0, pos1, pos2, pos3};
    int size_wp   = sizeof(waypoints)/sizeof(waypoints[0]);
    */

    // To ensure there is no problem for landing, you must set more waypoint than for landing
    Vector3f landing1( 0.0f,  0.0f, HEIGHT/2.0f);
    Vector3f landing2( 0.0f,  0.0f, HEIGHT/5.0f);
    Vector3f landing3( 0.0f,  0.0f, 0.0f);
    Vector3f landing[3] = {landing1, landing2, landing3};
    int size_land = sizeof(landing)/sizeof(landing[0]);

    // IMPORTANT TO MENTION
    //send a few setpoints before starting
    for(int i = 50; ros::ok() && i > 0; --i){
        local_pos_pub.publish(conversion_to_msg(pos));
        ros::spinOnce();
        rate.sleep();
    }

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
        if( current_state.mode  != "OFFBOARD" && (ros::Time::now() - last_request > ros::Duration(5.0))){
            if( set_mode_client.call(offb_set_mode) && offb_set_mode.response.mode_sent)
                ROS_INFO("Offboard enabled");
            last_request = ros::Time::now();
        } 
        else{
            if( !current_state.armed && (ros::Time::now() - last_request > ros::Duration(5.0))){
                if( arming_client.call(arm_cmd) && arm_cmd.response.success)
                    ROS_INFO("Vehicle armed");
                last_request = ros::Time::now();
            }
        }


        // Current location in vector form.
        pos = conversion_to_vect(est_local_pos);
        //ROS_INFO("Pos =[%f, %f, %f], idx=%d", pos(0),pos(1),pos(2),idx);

        if(!skip && AP_detected && !AP_verified){
        //if(AP_detected && !AP_verified){
            //AP_pos(0) = APtag_est_pos.poses[0].position.x;
            //AP_pos(1) = APtag_est_pos.poses[0].position.y;
            //AP_pos(2) = HEIGHT;
            AP_pos(0) = APtag_est_pos.detections[0].pose.pose.position.x;
            AP_pos(1) = APtag_est_pos.detections[0].pose.pose.position.y;
            AP_pos(2) = HEIGHT;
            //AP_pos(2) = APtag_est_pos.poses[0].position.z;

            goal_pos = to_center_pose(pos, AP_pos, OFFSET_CAM_X, OFFSET_CAM_Y);

            //ROS_INFO("AP  =[%f, %f, %f]", AP_pos(0),AP_pos(1),AP_pos(2));
            //ROS_INFO("Pos =[%f, %f, %f]", pos(0),pos(1),pos(2));
            //ROS_INFO("Goal=[%f, %f, %f]", goal_pos(0),goal_pos(1),goal_pos(2));
            //if(n_model>0)
            //ROS_INFO("TLP =[%f, %f, %f]", true_local_pos.pose[5].position.x,true_local_pos.pose[5].position.y,true_local_pos.pose[5].position.z);

            if(is_AP_centered(AP_pos, AP_POS_ACCEPT_X, AP_POS_ACCEPT_Y)){
                //ROS_INFO("YEAH YOUR CENTERED");
                if(check_id(DESIRED_ID)){
                    ROS_INFO("ID IS GOOD");
                    AP_verified = true;
                    AP_pos_save = goal_pos;                  
                    // Go to landing
                }
                else{
                    ROS_INFO("ID IS WRONG");
                        if (takeoff_done)
                            skip = true;
                }
            }
            local_pos_pub.publish(conversion_to_msg(goal_pos));
        }
        else{
            if(arming_client.call(arm_cmd) && is_goal_reached(goal_pos, pos, POS_ACCEPT_RAD)){
                if(AP_verified) ;
                else if(idx==0 && !takeoff_done){
                    takeoff_done = true;
                    idx = 0; // To reset for waypoints array
                }
                else if(takeoff_done && idx == (size_wp-1)){
                    traj_done = true;
                    idx = 0;
                }
                else if(takeoff_done && traj_done && idx == (size_land-1)){
                    landing_done = true;
                    current_state.armed = false;
                }
                else if( takeoff_done && traj_done && landing_done)
                    idx = idx;
                else
                    idx++;
            }
                



            if(!current_state.armed && landing_done){
                arm_cmd.request.value = false;
                // Disarmed drone
                if( !arming_client.call(arm_cmd) && !arm_cmd.response.success)
                    ROS_INFO("Vehicle disarmed");
            }
            else{   
                if(takeoff_done){
                    ROS_INFO("TAKEOFF DONE");
                    if(AP_verified){
                        if(!landing_in_progress)
                            // Check the value of goal_pos with calculation
                            goal_pos = landing_on_SP(AP_pos_save, AP_id);
                        if(is_goal_reached(goal_pos, pos, POS_ACCEPT_RAD)){
                             landing_in_progress = true;
                             goal_pos = lands(pos, SP_SIZE_HEIGHT);
                             if (fabs(pos(2)-HEIGHT) < TOL)
                                current_state.armed = false;
                        }      
                        //Vector3f v = goal_pos;
                        //ROS_INFO("GOAL=[%f, %f, %f], idx=%d", v(0), v(1), v(2), idx);
                    }
                    else if(traj_done)  goal_pos = landing[idx];
                    else                goal_pos = waypoints[idx];
                }
                else                    goal_pos = takeoff;
      
                Vector3f v = goal_pos;
                ROS_INFO("GOAL2=[%f, %f, %f], idx=%d", v(0), v(1), v(2), idx);
                local_pos_pub.publish(conversion_to_msg(goal_pos));
            }
        }
        
        // Needed or your callbacks would never get called
        ros::spinOnce();
        // Sleep for the time remaining to have 10Hz publish rate
        rate.sleep();
    }

    return 0;
}


// ------------------------------------------------------------------------------------------------
void WP_generation(Vector3f p, int cycle, float w, float l, int angle, Vector3f *array, int size, int type){
    
    int i = 0; int m = 0;
    float a = 0.05; float b = 0.005; // Parameters for the spiral, found experimentaly
    Vector3f P0, P1, P2, P3;
    Vector4i k; k<<1,2,3,4;
    Vector4i z; z<<4,4,4,4;
    array[0]=p;


    // ==============================
    // PROBLEM WITH INDEX
    // ==============================
    while(i!=cycle){
        if(type == SQUARE){
            ROS_INFO("SQUARE TRAJECTORY");
            if(i == 0)  P0 << p(0)          , p(1)+(i+1)*w  , p(2);  
            else        P0 << p(0)-i*l      , p(1)+(i+1)*w  , p(2);
                        P1 << p(0)+(i+1)*l  , P0(1)         , p(2);
                        P2 << P1(0)         , p(1)-(i+1)*w  , p(2);
                        P3 << p(0)-(i+1)*l  , P2(1)         , p(2);
        }
        else if (type == RECT_VERT){
            ROS_INFO("RECT VERT TRAJECTORY");
            P0 << p(0)+(2*i)*w    , p(1)+l  , p(2);
            P1 << p(0)+(2*i+1)*w  , P0(1)   , p(2);
            P2 << P1(0)           , p(1)    , p(2);
            P3 << p(0)+(2*i+2)*w  , p(1)    , p(2);
        }
        else if (type == RECT_HORI){
            ROS_INFO("RECT HORI TRAJECTORY");
            P0 << p(0)+l , p(1)+(2*i)*w     , p(2);
            P1 << P0(0)  , p(1)+(2*i+1)*w   , p(2);
            P2 << p(0)   , P1(1)            , p(2);
            P3 << p(0)   , p(1)+(2*i+2)*w   , p(2);
        }
        else if (type == SPIRAL){
            ROS_INFO("SPIRAL TRAJECTORY");
            float x = (a+b*angle*m)*cos(m*angle*M_PI/180.0f);
            float y = (a+b*angle*m)*sin(m*angle*M_PI/180.0f);
            P0 << x, y, p(2);
        }
        else{ // Just in case
            ROS_INFO("DEFAULT TRAJECTORY");
            P0 <<  1.9f,  1.9f, HEIGHT;
            P1 <<  1.9f, -1.9f, HEIGHT;
            P2 << -1.9f, -1.9f, HEIGHT;
            P3 << -1.9f,  1.9f, HEIGHT;
        }

        if (type == SPIRAL){
            if(m % (360/angle) == 0) i++;
            m++;
        }
        else{
            Vector3f temp[4] = {P0, P1, P2, P3};
            k = i*z + k;
            for(int j=0; j<4; j++)
                array[int(k(j))] = temp[j]; 
            i++;
        }
    }
}



Vector3f lands(Vector3f a, float H){
    Vector3f v(0.0f, 0.0f, 0.0f);
    v(0) = a(0);
    v(1) = a(1);
    v(2) = H-0.1f;
    return v;
}

Vector3f landing_on_SP(Vector3f a, int id){
    Vector3f v(0.0f, 0.0f, 0.0f);
    if(id==0){
        v(0) = a(0) - AP_SIZE/2.0f - SP_SIZE_WIDTH/2.0f;
        v(1) = a(1) + AP_SIZE/2.0f + SP_SIZE_LENGTH/2.0f;
        v(2) = a(2);
    }
    else if (id==1){
        v(0) = a(0) - AP_SIZE/2.0f - SP_SIZE_LENGTH/2.0f;
        v(1) = a(1) + AP_SIZE/2.0f + SP_SIZE_WIDTH/2.0f;
        v(2) = a(2);
    }
    else if (id==2){
        v(0) = a(0) + AP_SIZE/2.0f + SP_SIZE_LENGTH/2.0f;
        v(1) = a(1) - AP_SIZE/2.0f + SP_SIZE_WIDTH/2.0f;
        v(2) = a(2);
    }
    else
        ROS_INFO("Not a defined tags");
    return v;
}

Vector3f to_center_pose(Vector3f real_world, Vector3f camera_world, float offset_x, float offset_y){
    Vector3f target_pos(0.0f, 0.0f, 0.0f);
    target_pos(0) = real_world(0)-camera_world(1)+offset_y;
    target_pos(1) = real_world(1)-camera_world(0)+offset_x;
    target_pos(2) = camera_world(2);
    return target_pos;
}

// To check is the drone is at the centered with the AP
bool is_AP_centered(Vector3f a, float tol_x, float tol_y){
    if( fabs(a(0)) < tol_x   &&   fabs(a(1)) < tol_y)   return true;
    else                                                return false;                
} 

// To check if you found the right id
bool check_id(int id){
    if(AP_id == id)     return true;
    else                return false;
}


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
    n_model = true_pos->pose.size();
    //if(n_model>0){
    //ROS_INFO("TRP =[%f, %f, %f]", true_pos->pose[5].position.x,
    //                              true_pos->pose[5].position.y,
    //                              true_pos->pose[5].position.z);
    //}
}

// Callback which will save the estimated local position of the Apriltag
//geometry_msgs::PoseArray APtag_est_pos;
//void APtag_est_pos_cb(const geometry_msgs::PoseArray::ConstPtr& AP_est_pos){
//    APtag_est_pos = *AP_est_pos;
//    n_AP = AP_est_pos->poses.size();
//    //ROS_INFO("n=%d", n_AP);
//    if(n_AP>0){
//        AP_detected = true;
//        //AP_in_verification = true;
//            //ROS_INFO("APtag est pos=[%f, %f, %f]",  AP_est_pos->poses[0].position.x,
//            //                                        AP_est_pos->poses[0].position.y,
//            //                                        AP_est_pos->poses[0].position.z);
//    }
//    else
//        AP_detected = false;
//
//}

// Callback which will save the estimated local position of the Apriltag
//geometry_msgs::PoseArray APtag_est_pos;
void APtag_est_pos_cb(const apriltags_ros::AprilTagDetectionArray::ConstPtr& AP_est_pos){
    APtag_est_pos = *AP_est_pos;
    n_AP = AP_est_pos->detections.size();

    //ROS_INFO("n=%d", n_AP);
    if (n_AP == 0){
        skip = false;
        AP_detected = false;
    }
    else{
        AP_id = AP_est_pos->detections[0].id;
        //ROS_INFO("ID=%d", AP_id);
        AP_detected = true;
        //AP_in_verification = true;
            //ROS_INFO("APtag est pos=[%f, %f, %f]",  AP_est_pos->.detections[0].pose.pose.position.x,
            //                                        AP_est_pos->.detections[0].pose.pose.position.y,
            //                                        AP_est_pos->.detections[0].pose.pose.position.z);
    }
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

