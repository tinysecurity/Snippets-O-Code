#Get the file from the source repository and pull it into the container
FROM ros:kilted-ros-base

ARG USERNAME=ros_dev
ARG USER_UID=1000
ARG USER_GID=$USER_UID



#Delete if user exists

RUN if id -u $USER_UID >/dev/null 2>&1; then userdel -r $(id -un $USER_UID); fi \
# 2. Delete the group if the GID (1000) is taken
    && if getent group $USER_GID >/dev/null 2>&1; then groupdel $(getent group $USER_GID | cut -d: -f1); fi \
# Create the user
    && groupadd --gid $USER_GID $USERNAME \
    && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME \
    #
    # [Optional] Add sudo support. Omit if you don't need to install software after connecting.
    && apt-get update \
    && apt-get install -y sudo \
    && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME
RUN apt-get update && apt-get upgrade -y
RUN apt-get install -y python3-pip
COPY /home/bingus/RobotArm-for-AutonomousLabs-ZEISS/Wide Angle Sensor/opt3101_project ~/


#******************************************
# CRAZY SHIT ZONE BEGIN
#*******************************************

#Install necessary packages and dependencies
RUN apt-get install -y python3 \
&& apt-get install -y python3-pip \
&& apt-get install -y python3-colcon-common-extensions \
&& apt-get install -y python3-opencv \
&& apt-get install -y i2c-tools \
&& apt-get install pyton3-lpgpio \
&& apt-get install -y python3-smbus \
&& apt-get install cmake \
&& apt-get gcc c++ 
RUN wget https://raw.githubusercontent.com/adafruit/Raspberry-Pi-Installer-Scripts/master/raspi-blinka.py \
&& -E env PATH=$PATH python3 raspi-blinka.py
ENV SHELL=/bin/bash
#Set up .bashrc
USER $USERNAME
RUN mkdir -p /home/$USERNAME/ros2_ws/src
WORKDIR /home/$USERNAME/ros2_ws
RUN rosdep update

#set up ROS
RUN echo "Container created"
#Set user and environment to begin in 

#Configure shell container

#*****************************
#* CRAZY SHIT ZONE END
#*****************************
RUN /bin/bash -c "source /opt/ros/kilted/setup.sh && colcon build"

RUN echo "source /opt/ros/kilted/setup.bash" >> /home/$USERNAME/.bashrc \
    && echo "source /home/$USERNAME/ros2_ws/install/setup.bash" >> /home/$USERNAME/.bashrc
USER root
COPY ./ros_entrypoint.sh /
RUN chmod +x /ros_entrypoint.sh

USER $USERNAME
ENTRYPOINT ["/ros_entrypoint.sh"]
CMD ["/bin/bash"]
