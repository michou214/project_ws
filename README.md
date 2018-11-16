# project_ws

Tree of folder:

HOME (.bashrc => look at "my_bashrc.txt")
-- project_ws (=> clone this repository)
-- Firmware (=> See instruction below)
-- src
-- -- sitl_gazebo


For the Firmware part:
1) git clone the Firmware with the tag v.1.8.0
2) cd Firmware
3) git tag -l
4) git checkout -b tags/v1.8.0
5) git checkout v1.8.0
6) git submodule update --init --recursive
7) If you want to add custome models, put them in 
Firmware/Tools/sitl_gazebo/models

