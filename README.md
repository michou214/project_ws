# project_ws

## Tree of folders

* HOME (`.bashrc` => look at "my_bashrc.txt")
* * project_ws (=> clone this repository)
* * Firmware (=> See instruction below)
* * src
* * * sitl_gazebo


For the Firmware part:
1. `git clone` the Firmware with the tag v.1.8.0
```
cd Firmware
git tag -l
git checkout -b tags/v1.8.0
git checkout v1.8.0
git submodule update --init --recursive
```
2. If you want to add custome models, put them in 
`Firmware/Tools/sitl_gazebo/models`

## To be done [16.11.18]
- [X] Fix the subscribing problem (1)
- [ ] Use AP info to move the drone
- [ ] Test and debug

### Problem (1)
Understand why the use of the variable `APtag_est_pos` of type `geometry_msgs/PoseArray Message` doesn't work when I try to get access to the position x with:
* `AP_est_pos->poses[2].position.x` in the callback function where `APtag_est_pos = *AP_est_pos;`)
* `APtag_est_pos.poses[2].position.x` in the code 
