## project_ws

# Tree of folder:

HOME (.bashrc => look at "my_bashrc.txt")
* project_ws (=> clone this repository)
* Firmware (=> See instruction below)
* src
* * sitl_gazebo


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
Firmware/Tools/sitl_gazebo/models

