# Unitree Z1 Docker ROS1 Simulation Guide

本文档记录如何在 **Ubuntu 22.04 / Ubuntu 24.04 主机** 上，通过 **Docker** 运行 **ROS1 Noetic / Gazebo Classic** 环境，从零搭建并运行 **Unitree Z1 机械臂仿真**。

适用场景：

- 主机系统是 Ubuntu 22.04 或 Ubuntu 24.04。
- 主机上只有 ROS2，例如 Humble 或 Jazzy。
- 不想重装 Ubuntu 18.04 / 20.04。
- 不想在主机系统里混装 ROS1。
- 想跑官方 Z1 仿真：`unitree_ros + z1_controller + z1_sdk`。

核心思路：

```text
Ubuntu 22.04 / 24.04 主机
    |
    | Docker
    v
Ubuntu 20.04 + ROS1 Noetic + Gazebo Classic 容器
    |
    v
unitree_ros + z1_controller + z1_sdk
    |
    v
Z1 Gazebo 仿真
```

> 说明：Z1 官方仿真主要是 ROS1 生态，启动命令是 `roslaunch unitree_gazebo z1.launch`。如果主机是 Ubuntu 24.04 + ROS2 Jazzy，不建议直接在主机上硬装 ROS1 Noetic/Melodic，推荐用 Docker 隔离环境。

---

## 目录

- [1. 环境说明](#1-环境说明)
- [2. 安装 Docker](#2-安装-docker)
- [3. 安装图形转发依赖](#3-安装图形转发依赖)
- [4. 解决 Docker Hub 网络问题](#4-解决-docker-hub-网络问题)
- [5. 创建 Z1 Docker 工作目录](#5-创建-z1-docker-工作目录)
- [6. 创建 Dockerfile](#6-创建-dockerfile)
- [7. 创建自动安装脚本](#7-创建自动安装脚本)
- [8. 构建 Docker 镜像](#8-构建-docker-镜像)
- [9. 启动 Docker 容器](#9-启动-docker-容器)
- [10. 在容器内下载并编译 Z1 仿真代码](#10-在容器内下载并编译-z1-仿真代码)
- [11. 运行 Z1 仿真](#11-运行-z1-仿真)
- [12. 每次重新运行仿真的命令](#12-每次重新运行仿真的命令)
- [13. 常见问题与解决方法](#13-常见问题与解决方法)
- [14. Ubuntu 22.04 和 24.04 的区别](#14-ubuntu-2204-和-2404-的区别)
- [15. 相关代码库说明](#15-相关代码库说明)
- [16. 最短命令回顾](#16-最短命令回顾)
- [17. 上传到 GitHub](#17-上传到-github)

---

## 1. 环境说明

推荐主机环境：

```text
Ubuntu 22.04 + ROS2 Humble
或
Ubuntu 24.04 + ROS2 Jazzy
```

容器环境：

```text
Ubuntu 20.04
ROS1 Noetic
Gazebo Classic
catkin
```

Z1 仿真代码：

```text
unitree_ros
unitree_ros_to_real/unitree_legged_msgs
z1_controller
z1_sdk
```

为什么不用主机原生环境？

```text
ROS Melodic -> Ubuntu 18.04
ROS Noetic  -> Ubuntu 20.04
ROS Humble  -> Ubuntu 22.04
ROS Jazzy   -> Ubuntu 24.04
```

Unitree Z1 官方仿真是 ROS1 路线，而 Ubuntu 22.04 / 24.04 主机通常是 ROS2 路线。用 Docker 可以避免 ROS1 和 ROS2 依赖互相污染。

---

## 2. 安装 Docker

### 2.1 推荐方式：安装 Docker CE

如果可以访问 Docker 官方源，执行：

```bash
sudo apt update
sudo apt install -y ca-certificates curl gnupg lsb-release

sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc

echo \
"deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu \
$(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}") stable" | \
sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

sudo systemctl enable docker
sudo systemctl start docker
```

检查：

```bash
docker --version
```

如果能输出 Docker 版本，说明 Docker 已安装。

### 2.2 如果官方 Docker 源连接失败：换清华 Docker CE 源

如果出现：

```text
curl: (35) OpenSSL SSL_connect: 连接被对方重置 in connection to download.docker.com:443
chmod: 无法访问 '/etc/apt/keyrings/docker.asc': 没有那个文件或目录
```

说明访问 Docker 官方源失败，GPG key 没有下载下来。这个问题可以通过换用清华 Docker CE 镜像源解决。

先清理刚才失败留下的配置：

```bash
sudo rm -f /etc/apt/sources.list.d/docker.list
sudo rm -f /etc/apt/keyrings/docker.gpg
sudo rm -f /etc/apt/keyrings/docker.asc
```

安装基础工具：

```bash
sudo apt update
sudo apt install -y ca-certificates curl gnupg lsb-release
```

创建 keyrings 目录：

```bash
sudo install -m 0755 -d /etc/apt/keyrings
```

使用清华 Docker CE 源下载 GPG key：

```bash
curl -fsSL https://mirrors.tuna.tsinghua.edu.cn/docker-ce/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
sudo chmod a+r /etc/apt/keyrings/docker.gpg
```

添加清华 Docker CE apt 源：

```bash
echo \
"deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://mirrors.tuna.tsinghua.edu.cn/docker-ce/linux/ubuntu \
$(. /etc/os-release && echo "$VERSION_CODENAME") stable" | \
sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
```

安装 Docker：

```bash
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

sudo systemctl enable docker
sudo systemctl start docker

docker --version
```

如果清华源仍然失败，再使用 Ubuntu 自带的 `docker.io` 作为兜底：

```bash
sudo rm -f /etc/apt/sources.list.d/docker.list
sudo rm -f /etc/apt/keyrings/docker.gpg
sudo rm -f /etc/apt/keyrings/docker.asc

sudo apt update
sudo apt install -y docker.io

sudo systemctl enable docker
sudo systemctl start docker

docker --version
```

`docker.io` 不一定是最新版 Docker，但足够运行本项目。

### 2.3 配置当前用户使用 Docker

执行：

```bash
sudo usermod -aG docker $USER
newgrp docker
```

测试：

```bash
docker ps
```

如果不报权限错误，说明当前用户可以使用 Docker。

如果仍然报：

```text
permission denied while trying to connect to the docker API at unix:///var/run/docker.sock
```

可以先用：

```bash
sudo docker ps
```

后续所有 Docker 命令临时加 `sudo`。

---

## 3. 安装图形转发依赖

Gazebo 是图形界面程序，虽然它运行在 Docker 容器里，但窗口需要显示到主机 Ubuntu 桌面上。因此主机需要安装 X11 图形转发相关工具。

在主机终端执行：

```bash
sudo apt update
sudo apt install -y x11-xserver-utils mesa-utils
```

允许 Docker 容器里的 root 用户访问主机 X11 显示服务：

```bash
xhost +local:root
```

正常会输出类似：

```text
non-network local connections being added to access control list
```

如果后续每次重启电脑后 Gazebo 打不开窗口，可以重新执行：

```bash
xhost +local:root
```

如果你使用的是 Ubuntu 24.04，默认桌面可能是 Wayland。Gazebo Classic 在 Docker 里通过 X11 显示时，Wayland 环境有时会出现窗口打不开、黑屏或 OpenGL 报错。遇到这种情况，建议注销后在登录界面选择：

```text
Ubuntu on Xorg
```

再登录，并重新执行：

```bash
xhost +local:root
```

为了提高兼容性，本文后面的 `docker run` 命令中保留了：

```bash
-e LIBGL_ALWAYS_SOFTWARE=1
```

这会让 Gazebo 使用软件渲染。优点是更稳，缺点是速度可能慢一点。如果你的显卡驱动和 Docker 图形转发都正常，可以后续尝试去掉这一项。

---

## 4. 解决 Docker Hub 网络问题

如果执行：

```bash
docker run hello-world
```

出现：

```text
failed to resolve reference "docker.io/library/hello-world:latest"
Head "https://registry-1.docker.io/v2/library/hello-world/manifests/latest":
dial tcp ...:443: i/o timeout
```

说明 Docker 已安装成功，但无法访问 Docker Hub。

配置镜像加速：

```bash
sudo mkdir -p /etc/docker
sudo nano /etc/docker/daemon.json
```

写入：

```json
{
  "registry-mirrors": [
    "https://docker.m.daocloud.io",
    "https://docker.1ms.run"
  ]
}
```

重启 Docker：

```bash
sudo systemctl daemon-reload
sudo systemctl restart docker
```

检查：

```bash
docker info
```

如果输出中有：

```text
Registry Mirrors:
 https://docker.m.daocloud.io/
 https://docker.1ms.run/
```

说明镜像源已生效。

也可以直接测试拉 ROS 镜像：

```bash
docker pull docker.m.daocloud.io/osrf/ros:noetic-desktop-full
```

如果成功，后续 Dockerfile 的第一行就使用：

```dockerfile
FROM docker.m.daocloud.io/osrf/ros:noetic-desktop-full
```

---

## 5. 创建 Z1 Docker 工作目录

在主机终端执行：

```bash
mkdir -p ~/unitree_z1_docker
cd ~/unitree_z1_docker
```

最终目录结构大致为：

```text
~/unitree_z1_docker/
  Dockerfile
  setup_z1_noetic.sh
  unitree_ws/
  unitree_z1/
  third_party/
```

---

## 6. 创建 Dockerfile

在主机中创建：

```bash
cd ~/unitree_z1_docker
nano Dockerfile
```

写入：

```dockerfile
FROM docker.m.daocloud.io/osrf/ros:noetic-desktop-full

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

RUN apt-get update && apt-get install -y \
    git \
    curl \
    wget \
    nano \
    vim \
    cmake \
    build-essential \
    pkg-config \
    libboost-dev \
    libeigen3-dev \
    python3-pip \
    python3-rosdep \
    python3-catkin-tools \
    python3-vcstool \
    python3-osrf-pycommon \
    ros-noetic-controller-interface \
    ros-noetic-gazebo-ros-control \
    ros-noetic-joint-state-controller \
    ros-noetic-effort-controllers \
    ros-noetic-joint-trajectory-controller \
    ros-noetic-controller-manager \
    ros-noetic-ros-control \
    ros-noetic-ros-controllers \
    ros-noetic-robot-state-publisher \
    ros-noetic-joint-state-publisher \
    ros-noetic-joint-state-publisher-gui \
    ros-noetic-xacro \
    ros-noetic-tf \
    ros-noetic-tf2-ros \
    ros-noetic-geometry-msgs \
    ros-noetic-std-msgs \
    ros-noetic-sensor-msgs \
    ros-noetic-nav-msgs \
    ros-noetic-rviz \
    && rm -rf /var/lib/apt/lists/*

RUN rosdep init || true

RUN echo "source /opt/ros/noetic/setup.bash" >> /root/.bashrc
RUN echo "if [ -f /root/work/unitree_ws/devel/setup.bash ]; then source /root/work/unitree_ws/devel/setup.bash; fi" >> /root/.bashrc
RUN echo "export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:/root/work/unitree_z1/z1_sdk/lib:/root/work/unitree_z1/z1_controller/lib" >> /root/.bashrc

WORKDIR /root/work

CMD ["bash"]
```

保存：

```text
Ctrl + O
Enter
Ctrl + X
```

> 注意：`FROM docker.m.daocloud.io/osrf/ros:noetic-desktop-full` 是 Dockerfile 的第一行，不是在终端里单独执行的命令。

如果你网络能直接访问 Docker Hub，也可以改成：

```dockerfile
FROM osrf/ros:noetic-desktop-full
```

---

## 7. 创建自动安装脚本

在主机执行：

```bash
cd ~/unitree_z1_docker
nano setup_z1_noetic.sh
```

写入：

```bash
#!/usr/bin/env bash
set -e

echo "==> Source ROS Noetic"
source /opt/ros/noetic/setup.bash

echo "==> Update rosdep"
rosdep update || true

echo "==> Create catkin workspace"
mkdir -p /root/work/unitree_ws/src
cd /root/work/unitree_ws/src

echo "==> Clone unitree_ros"
if [ ! -d unitree_ros ]; then
  git clone https://github.com/unitreerobotics/unitree_ros.git
else
  echo "unitree_ros already exists, skip clone"
fi

echo "==> Clone unitree_ros_to_real for unitree_legged_msgs"
mkdir -p /root/work/third_party
cd /root/work/third_party

if [ ! -d unitree_ros_to_real ]; then
  git clone https://github.com/unitreerobotics/unitree_ros_to_real.git
else
  echo "unitree_ros_to_real already exists, skip clone"
fi

echo "==> Copy unitree_legged_msgs into workspace"
if [ ! -d /root/work/unitree_ws/src/unitree_legged_msgs ]; then
  cp -r /root/work/third_party/unitree_ros_to_real/unitree_legged_msgs /root/work/unitree_ws/src/
else
  echo "unitree_legged_msgs already exists, skip copy"
fi

echo "==> Install ROS dependencies"
cd /root/work/unitree_ws
rosdep install --from-paths src --ignore-src -r -y || true

echo "==> Build unitree_ws"
catkin_make

echo "==> Source unitree_ws"
source /root/work/unitree_ws/devel/setup.bash

echo "==> Clone z1_controller and z1_sdk"
mkdir -p /root/work/unitree_z1
cd /root/work/unitree_z1

if [ ! -d z1_controller ]; then
  git clone https://github.com/unitreerobotics/z1_controller.git
else
  echo "z1_controller already exists, skip clone"
fi

if [ ! -d z1_sdk ]; then
  git clone https://github.com/unitreerobotics/z1_sdk.git
else
  echo "z1_sdk already exists, skip clone"
fi

echo "==> Build z1_controller"
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash
cd /root/work/unitree_z1/z1_controller
mkdir -p build
cd build
cmake ..
make -j"$(nproc)"

echo "==> Build z1_sdk"
cd /root/work/unitree_z1/z1_sdk
mkdir -p build
cd build
cmake ..
make -j"$(nproc)"

echo "==> Add library path to current shell"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/root/work/unitree_z1/z1_sdk/lib:/root/work/unitree_z1/z1_controller/lib

echo "==> Check important files"
echo "unitree_gazebo:"
rospack find unitree_gazebo || true

echo "z1_description:"
rospack find z1_description || true

echo "z1_controller build outputs:"
ls -lh /root/work/unitree_z1/z1_controller/build/sim_ctrl /root/work/unitree_z1/z1_controller/build/z1_ctrl || true

echo "z1_sdk build outputs:"
ls -lh /root/work/unitree_z1/z1_sdk/build/highcmd_basic \
       /root/work/unitree_z1/z1_sdk/build/highcmd_development \
       /root/work/unitree_z1/z1_sdk/build/lowcmd_development \
       /root/work/unitree_z1/z1_sdk/build/lowcmd_multirobots || true

echo "==> Done."
echo "Next:"
echo "  Terminal 1: roslaunch unitree_gazebo z1.launch"
echo "  Terminal 2: cd /root/work/unitree_z1/z1_controller/build && ./sim_ctrl"
echo "  Terminal 3: cd /root/work/unitree_z1/z1_sdk/build && ./highcmd_basic"
```

保存并赋予执行权限：

```bash
chmod +x setup_z1_noetic.sh
```

---

## 8. 构建 Docker 镜像

在主机执行：

```bash
cd ~/unitree_z1_docker
docker build -t unitree-z1-noetic .
```

如果 Docker 权限不足：

```bash
sudo docker build -t unitree-z1-noetic .
```

检查镜像：

```bash
docker images | grep unitree-z1-noetic
```

正常输出类似：

```text
unitree-z1-noetic   latest   ...   ...
```

---

## 9. 启动 Docker 容器

先允许 Docker 容器访问图形界面：

```bash
xhost +local:root
```

启动容器：

```bash
docker run -it \
  --name unitree-z1-noetic \
  --net=host \
  --privileged \
  -e DISPLAY=$DISPLAY \
  -e QT_X11_NO_MITSHM=1 \
  -e LIBGL_ALWAYS_SOFTWARE=1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  -v ~/unitree_z1_docker:/root/work \
  unitree-z1-noetic \
  bash
```

如果 Docker 权限不足：

```bash
sudo docker run -it \
  --name unitree-z1-noetic \
  --net=host \
  --privileged \
  -e DISPLAY=$DISPLAY \
  -e QT_X11_NO_MITSHM=1 \
  -e LIBGL_ALWAYS_SOFTWARE=1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  -v ~/unitree_z1_docker:/root/work \
  unitree-z1-noetic \
  bash
```

进入容器后，命令行提示符应类似：

```text
root@主机名:~/work#
```

---

## 10. 在容器内下载并编译 Z1 仿真代码

在容器内执行：

```bash
cd /root/work
./setup_z1_noetic.sh
```

脚本会自动完成：

```text
1. 下载 unitree_ros
2. 下载 unitree_ros_to_real
3. 拷贝 unitree_legged_msgs 到 catkin 工作空间
4. 编译 unitree_ws
5. 下载 z1_controller
6. 下载 z1_sdk
7. 编译 z1_controller
8. 编译 z1_sdk
```

成功后应能看到：

```text
/root/work/unitree_z1/z1_controller/build/sim_ctrl
/root/work/unitree_z1/z1_controller/build/z1_ctrl
/root/work/unitree_z1/z1_sdk/build/highcmd_basic
```

如果脚本中途失败，参考 [常见问题与解决方法](#13-常见问题与解决方法)。

---

## 11. 运行 Z1 仿真

需要同时开 3 个终端。

### 终端 1：启动 Gazebo 和 Z1 模型

主机新开终端：

```bash
docker start unitree-z1-noetic
docker exec -it unitree-z1-noetic bash
```

如果权限不足：

```bash
sudo docker start unitree-z1-noetic
sudo docker exec -it unitree-z1-noetic bash
```

进入容器后：

```bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash
roslaunch unitree_gazebo z1.launch
```

正常情况会打开 Gazebo，并显示 Z1 机械臂模型。

### 终端 2：启动 Z1 仿真控制桥

主机新开第二个终端：

```bash
docker exec -it unitree-z1-noetic bash
```

进入容器后：

```bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash
cd /root/work/unitree_z1/z1_controller/build
ls -lh sim_ctrl
./sim_ctrl
```

终端 2 不要关闭。它负责连接 SDK 和 Gazebo。

### 终端 3：运行 SDK 控制示例

主机新开第三个终端：

```bash
docker exec -it unitree-z1-noetic bash
```

进入容器后：

```bash
cd /root/work/unitree_z1/z1_sdk/build
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/root/work/unitree_z1/z1_sdk/lib:/root/work/unitree_z1/z1_controller/lib
ls -lh highcmd_basic
./highcmd_basic
```

如果一切正常，Gazebo 中的 Z1 会开始响应 SDK 示例命令。

运行顺序必须是：

```text
1. roslaunch unitree_gazebo z1.launch
2. ./sim_ctrl
3. ./highcmd_basic
```

---

## 12. 每次重新运行仿真的命令

以后不需要重新 build，只需要启动容器并分别运行三个终端。

终端 1：

```bash
docker start unitree-z1-noetic
docker exec -it unitree-z1-noetic bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash
roslaunch unitree_gazebo z1.launch
```

终端 2：

```bash
docker exec -it unitree-z1-noetic bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash
cd /root/work/unitree_z1/z1_controller/build
./sim_ctrl
```

终端 3：

```bash
docker exec -it unitree-z1-noetic bash
cd /root/work/unitree_z1/z1_sdk/build
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/root/work/unitree_z1/z1_sdk/lib:/root/work/unitree_z1/z1_controller/lib
./highcmd_basic
```

如果 Docker 权限不足，将每个 `docker` 改成 `sudo docker`。

---

## 13. 常见问题与解决方法

本节整理了实际搭建过程中遇到的问题和处理方式。

### 13.1 Docker 官方源连接失败，改用清华 Docker CE 源

错误：

```text
curl: (35) OpenSSL SSL_connect: 连接被对方重置 in connection to download.docker.com:443
chmod: 无法访问 '/etc/apt/keyrings/docker.asc': 没有那个文件或目录
```

原因：

```text
访问 download.docker.com 失败，GPG key 没有下载下来。
```

优先解决方法：换用清华 Docker CE 镜像源。

```bash
sudo rm -f /etc/apt/sources.list.d/docker.list
sudo rm -f /etc/apt/keyrings/docker.gpg
sudo rm -f /etc/apt/keyrings/docker.asc

sudo apt update
sudo apt install -y ca-certificates curl gnupg lsb-release

sudo install -m 0755 -d /etc/apt/keyrings

curl -fsSL https://mirrors.tuna.tsinghua.edu.cn/docker-ce/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
sudo chmod a+r /etc/apt/keyrings/docker.gpg

echo \
"deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://mirrors.tuna.tsinghua.edu.cn/docker-ce/linux/ubuntu \
$(. /etc/os-release && echo "$VERSION_CODENAME") stable" | \
sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

sudo systemctl enable docker
sudo systemctl start docker

docker --version
```

如果清华源仍然不可用，可以再退回 Ubuntu 自带的 `docker.io`：

```bash
sudo rm -f /etc/apt/sources.list.d/docker.list
sudo rm -f /etc/apt/keyrings/docker.gpg
sudo rm -f /etc/apt/keyrings/docker.asc

sudo apt update
sudo apt install -y docker.io

sudo systemctl enable docker
sudo systemctl start docker

docker --version
```

---

### 13.2 Docker 已安装，但拉镜像超时

错误：

```text
failed to resolve reference "docker.io/library/hello-world:latest"
Head "https://registry-1.docker.io/v2/library/hello-world/manifests/latest":
dial tcp ...:443: i/o timeout
```

原因：

```text
Docker 本体已安装成功，但访问 Docker Hub 超时。
```

解决：

```bash
sudo mkdir -p /etc/docker
sudo nano /etc/docker/daemon.json
```

写入：

```json
{
  "registry-mirrors": [
    "https://docker.m.daocloud.io",
    "https://docker.1ms.run"
  ]
}
```

重启：

```bash
sudo systemctl daemon-reload
sudo systemctl restart docker
```

测试：

```bash
docker pull docker.m.daocloud.io/osrf/ros:noetic-desktop-full
```

Dockerfile 第一行使用：

```dockerfile
FROM docker.m.daocloud.io/osrf/ros:noetic-desktop-full
```

---

### 13.3 `FROM docker.m.daocloud.io/osrf/ros:noetic-desktop-full` 写在哪里？

它写在 `Dockerfile` 的第一行，不是在终端里运行。

正确位置：

```dockerfile
FROM docker.m.daocloud.io/osrf/ros:noetic-desktop-full

ENV DEBIAN_FRONTEND=noninteractive
...
```

如果已经 `docker build` 成功，后面才发现忘记改这一行，通常不用重做。因为 Dockerfile 只在构建镜像时生效。

只有当 `docker build` 因为拉不到 `osrf/ros:noetic-desktop-full` 失败时，才需要改第一行并重新构建：

```bash
docker build -t unitree-z1-noetic .
```

---

### 13.4 `unitree_legged_msgs` 重复

错误：

```text
Multiple packages found with the same name "unitree_legged_msgs":
- unitree_legged_msgs
- unitree_ros_to_real/unitree_legged_msgs
```

原因：

```text
unitree_ros_to_real 被 clone 到 unitree_ws/src 下面，同时 unitree_legged_msgs 又被复制了一份到 unitree_ws/src。
catkin 在 src 里发现两个同名包。
```

解决：

```bash
cd /root/work/unitree_ws/src
mv unitree_ros_to_real /root/work/unitree_ros_to_real_backup

cd /root/work/unitree_ws
source /opt/ros/noetic/setup.bash
catkin_make
source /root/work/unitree_ws/devel/setup.bash
```

本文档中的 `setup_z1_noetic.sh` 已经修复这个问题：`unitree_ros_to_real` 被放在 `/root/work/third_party`，不会留在 `unitree_ws/src` 里。

---

#### `unitree_legged_msgs` 重复包的完整修复流程

如果第 9/10 步没有成功跑完，并出现：

```text
Multiple packages found with the same name "unitree_legged_msgs":
- unitree_legged_msgs
- unitree_ros_to_real/unitree_legged_msgs
```

说明 ROS 工作空间里出现了两个同名包：

```text
/root/work/unitree_ws/src/unitree_legged_msgs
/root/work/unitree_ws/src/unitree_ros_to_real/unitree_legged_msgs
```

原因是旧脚本同时做了两件事：

```text
1. 把 unitree_ros_to_real 整个仓库 clone 到 unitree_ws/src 里面
2. 又把 unitree_ros_to_real/unitree_legged_msgs 复制了一份到 unitree_ws/src 里面
```

于是 catkin 在 `src` 目录里扫描到了两个 `unitree_legged_msgs`，所以 `catkin_make` 会直接停止。

下面这些只是警告，不是导致失败的原因：

```text
Warning: running 'rosdep update' as root is not recommended
Package name "aliengoZ1_description" does not follow the naming conventions
```

如果当前还在 Docker 容器里，提示符类似：

```bash
root@lzj:~/work#
```

先把重复的 `unitree_ros_to_real` 仓库移出 ROS 工作空间：

```bash
cd /root/work/unitree_ws/src
mv unitree_ros_to_real /root/work/unitree_ros_to_real_backup
```

然后重新编译 ROS 工作空间：

```bash
cd /root/work/unitree_ws
source /opt/ros/noetic/setup.bash
catkin_make
```

如果成功，最后通常能看到类似：

```text
[100%] Built target ...
```

加载工作空间环境：

```bash
source /root/work/unitree_ws/devel/setup.bash
```

检查包是否能找到：

```bash
rospack find unitree_gazebo
rospack find z1_description
```

正常应该输出类似：

```text
/root/work/unitree_ws/src/unitree_ros/unitree_gazebo
/root/work/unitree_ws/src/unitree_ros/robots/z1_description
```

因为脚本在 `catkin_make` 失败后会停住，所以后面的 `z1_controller` 和 `z1_sdk` 可能还没有下载或编译。继续执行：

```bash
mkdir -p /root/work/unitree_z1
cd /root/work/unitree_z1
```

下载 `z1_controller`：

```bash
if [ ! -d z1_controller ]; then
  git clone https://github.com/unitreerobotics/z1_controller.git
fi
```

下载 `z1_sdk`：

```bash
if [ ! -d z1_sdk ]; then
  git clone https://github.com/unitreerobotics/z1_sdk.git
fi
```

编译 `z1_controller`：

```bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash

cd /root/work/unitree_z1/z1_controller
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

检查是否生成 `sim_ctrl`：

```bash
ls -lh sim_ctrl z1_ctrl
```

正常应该能看到：

```text
sim_ctrl
z1_ctrl
```

继续编译 `z1_sdk`：

```bash
cd /root/work/unitree_z1/z1_sdk
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

检查 SDK 示例是否生成：

```bash
ls -lh highcmd_basic highcmd_development lowcmd_development lowcmd_multirobots
```

正常应该能看到：

```text
highcmd_basic
highcmd_development
lowcmd_development
lowcmd_multirobots
```

为了避免下次脚本又出现同样问题，主机上的脚本：

```text
~/unitree_z1_docker/setup_z1_noetic.sh
```

不要把 `unitree_ros_to_real` clone 到 `unitree_ws/src` 里面。应该把它放到工作空间外面，例如：

```text
/root/work/third_party/unitree_ros_to_real
```

在主机新开终端：

```bash
cd ~/unitree_z1_docker
nano setup_z1_noetic.sh
```

如果脚本里还有下面这种旧写法：

```bash
cd /root/work/unitree_ws/src

echo "==> Clone unitree_ros_to_real for unitree_legged_msgs"
if [ ! -d unitree_ros_to_real ]; then
  git clone https://github.com/unitreerobotics/unitree_ros_to_real.git
else
  echo "unitree_ros_to_real already exists, skip clone"
fi

echo "==> Copy unitree_legged_msgs into workspace"
if [ ! -d /root/work/unitree_ws/src/unitree_legged_msgs ]; then
  cp -r /root/work/unitree_ws/src/unitree_ros_to_real/unitree_legged_msgs /root/work/unitree_ws/src/
else
  echo "unitree_legged_msgs already exists, skip copy"
fi
```

把它改成：

```bash
echo "==> Clone unitree_ros_to_real for unitree_legged_msgs"
mkdir -p /root/work/third_party
cd /root/work/third_party

if [ ! -d unitree_ros_to_real ]; then
  git clone https://github.com/unitreerobotics/unitree_ros_to_real.git
else
  echo "unitree_ros_to_real already exists, skip clone"
fi

echo "==> Copy unitree_legged_msgs into workspace"
if [ ! -d /root/work/unitree_ws/src/unitree_legged_msgs ]; then
  cp -r /root/work/third_party/unitree_ros_to_real/unitree_legged_msgs /root/work/unitree_ws/src/
else
  echo "unitree_legged_msgs already exists, skip copy"
fi

cd /root/work/unitree_ws/src
```

保存退出：

```text
Ctrl + O
Enter
Ctrl + X
```

本文档第 7 节给出的 `setup_z1_noetic.sh` 已经采用了这个修正版写法。

如果你不想分段执行，也可以在容器里直接运行下面这一组最省事的修复命令：

```bash
cd /root/work/unitree_ws/src
mv unitree_ros_to_real /root/work/unitree_ros_to_real_backup

cd /root/work/unitree_ws
source /opt/ros/noetic/setup.bash
catkin_make
source /root/work/unitree_ws/devel/setup.bash

mkdir -p /root/work/unitree_z1
cd /root/work/unitree_z1

if [ ! -d z1_controller ]; then
  git clone https://github.com/unitreerobotics/z1_controller.git
fi

if [ ! -d z1_sdk ]; then
  git clone https://github.com/unitreerobotics/z1_sdk.git
fi

cd /root/work/unitree_z1/z1_controller
mkdir -p build
cd build
cmake ..
make -j$(nproc)

cd /root/work/unitree_z1/z1_sdk
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

这组命令跑完如果没有报错，就可以继续使用三个终端启动 Z1 仿真：

终端 1：

```bash
docker exec -it unitree-z1-noetic bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash
roslaunch unitree_gazebo z1.launch
```

终端 2：

```bash
docker exec -it unitree-z1-noetic bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash
cd /root/work/unitree_z1/z1_controller/build
./sim_ctrl
```

终端 3：

```bash
docker exec -it unitree-z1-noetic bash
cd /root/work/unitree_z1/z1_sdk/build
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/root/work/unitree_z1/z1_sdk/lib:/root/work/unitree_z1/z1_controller/lib
./highcmd_basic
```

---

### 13.5 GitHub clone 失败

错误：

```text
fatal: unable to access 'https://github.com/unitreerobotics/z1_controller.git/':
GnuTLS recv error (-110): The TLS connection was non-properly terminated.
```

或：

```text
Failed to connect to github.com port 443: Connection timed out
```

原因：

```text
网络连接 GitHub 不稳定。
```

解决：

直接重试：

```bash
cd /root/work/unitree_z1

if [ ! -d z1_controller ]; then
  git clone https://github.com/unitreerobotics/z1_controller.git
fi

if [ ! -d z1_sdk ]; then
  git clone https://github.com/unitreerobotics/z1_sdk.git
fi
```

如果目录已经部分存在但不完整，可以删除后重试：

```bash
rm -rf /root/work/unitree_z1/z1_controller
git clone https://github.com/unitreerobotics/z1_controller.git /root/work/unitree_z1/z1_controller
```

---

### 13.6 `docker exec` 权限不足

错误：

```text
permission denied while trying to connect to the docker API at unix:///var/run/docker.sock
```

原因：

```text
当前用户还没有 Docker 权限，或用户组没有在当前终端生效。
```

解决：

```bash
sudo usermod -aG docker $USER
newgrp docker
docker ps
```

如果仍然失败，临时使用：

```bash
sudo docker ps
sudo docker exec -it unitree-z1-noetic bash
```

注意：如果 `docker exec` 没成功，后面的 `source /opt/ros/noetic/setup.bash` 会在主机上执行，而不是容器里执行，于是会出现：

```text
bash: /opt/ros/noetic/setup.bash: 没有那个文件或目录
```

这是因为还没有进入容器。

---

### 13.7 `sim_ctrl` 提示等待 `z1_sdk`

提示：

```text
[WARNING] UDPPort::recv, unblock version, connect with z1_sdk wait time out
[WARNING] Lose connection with z1_sdk
```

原因：

```text
sim_ctrl 已经启动，但还没有运行 z1_sdk 示例程序 highcmd_basic。
```

这是正常等待提示，不是编译错误。

解决：

新开第三个终端进入容器：

```bash
docker exec -it unitree-z1-noetic bash
```

运行：

```bash
cd /root/work/unitree_z1/z1_sdk/build
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/root/work/unitree_z1/z1_sdk/lib:/root/work/unitree_z1/z1_controller/lib
./highcmd_basic
```

确保三个终端顺序为：

```text
终端 1：roslaunch unitree_gazebo z1.launch
终端 2：./sim_ctrl
终端 3：./highcmd_basic
```

---

### 13.8 `sim_ctrl` 不存在

错误：

```text
ls: cannot access 'sim_ctrl': No such file or directory
```

原因：

```text
z1_controller 没有编译成功，或 cmake 时没有 source ROS 环境。
```

解决：

```bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash

cd /root/work/unitree_z1/z1_controller
mkdir -p build
cd build
cmake ..
make -j$(nproc)

ls -lh sim_ctrl z1_ctrl
```

---

### 13.9 `highcmd_basic` 不存在

错误：

```text
ls: cannot access 'highcmd_basic': No such file or directory
```

原因：

```text
z1_sdk 没有编译成功。
```

解决：

```bash
cd /root/work/unitree_z1/z1_sdk
mkdir -p build
cd build
cmake ..
make -j$(nproc)

ls -lh highcmd_basic highcmd_development lowcmd_development lowcmd_multirobots
```

---

### 13.10 Gazebo 窗口打不开

错误可能类似：

```text
cannot connect to X server
```

解决：

主机执行：

```bash
xhost +local:root
```

再进入容器启动 Gazebo。

如果 Ubuntu 24.04 使用 Wayland，建议注销后在登录界面选择：

```text
Ubuntu on Xorg
```

再登录，然后重新执行：

```bash
xhost +local:root
```

如果 Gazebo 黑屏或 OpenGL 报错，保留 Docker 启动参数：

```bash
-e LIBGL_ALWAYS_SOFTWARE=1
```

软件渲染会慢一点，但更稳定。

---

### 13.11 容器名已经存在

错误：

```text
docker: Error response from daemon: Conflict. The container name "/unitree-z1-noetic" is already in use
```

查看：

```bash
docker ps -a
```

如果容器只是停止了：

```bash
docker start unitree-z1-noetic
docker exec -it unitree-z1-noetic bash
```

如果想删除重建：

```bash
docker stop unitree-z1-noetic
docker rm unitree-z1-noetic
```

注意：代码保存在主机 `~/unitree_z1_docker`，删除容器不会删除这个目录。

---

## 14. Ubuntu 22.04 和 24.04 的区别

如果使用 Docker 方案，Ubuntu 22.04 和 Ubuntu 24.04 区别不大。

| 项目 | Ubuntu 22.04 | Ubuntu 24.04 | 影响 |
|---|---|---|---|
| 主机 ROS2 | 常见 Humble | 常见 Jazzy | 不影响容器里的 ROS1 |
| Docker | 支持 | 支持 | 基本一样 |
| 图形界面 | Wayland/Xorg | 更常见 Wayland | 24.04 更可能需要切 Xorg |
| Z1 仿真 | 容器内运行 | 容器内运行 | 基本无差别 |

结论：

```text
Ubuntu 22.04 / 24.04 主机都推荐用 Docker 跑 ROS1 Noetic 容器。
不要在主机上直接硬装 ROS1 Noetic/Melodic。
```

---

## 15. 相关代码库说明

### `unitree_ros`

作用：

```text
ROS1 + Gazebo 仿真包，提供 Unitree 机器人模型、Gazebo launch、控制器配置。
Z1 仿真入口是 roslaunch unitree_gazebo z1.launch。
```

仓库：

```text
https://github.com/unitreerobotics/unitree_ros
```

### `unitree_ros_to_real`

作用：

```text
提供 unitree_legged_msgs。
unitree_ros 编译时需要这个消息包。
```

仓库：

```text
https://github.com/unitreerobotics/unitree_ros_to_real
```

注意：

```text
不要把整个 unitree_ros_to_real 仓库放在 unitree_ws/src 里。
只需要复制 unitree_legged_msgs 到 unitree_ws/src。
否则会出现 unitree_legged_msgs 重复包错误。
```

### `z1_controller`

作用：

```text
Z1 控制桥。
仿真时运行 ./sim_ctrl。
实物控制时才考虑 ./z1_ctrl。
```

仓库：

```text
https://github.com/unitreerobotics/z1_controller
```

### `z1_sdk`

作用：

```text
Z1 SDK 示例和接口。
最常用入门示例是 highcmd_basic。
```

仓库：

```text
https://github.com/unitreerobotics/z1_sdk
```

---

## 16. 最短命令回顾

构建并启动：

```bash
mkdir -p ~/unitree_z1_docker
cd ~/unitree_z1_docker

# 创建 Dockerfile 和 setup_z1_noetic.sh 后
docker build -t unitree-z1-noetic .

xhost +local:root

docker run -it \
  --name unitree-z1-noetic \
  --net=host \
  --privileged \
  -e DISPLAY=$DISPLAY \
  -e QT_X11_NO_MITSHM=1 \
  -e LIBGL_ALWAYS_SOFTWARE=1 \
  -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
  -v ~/unitree_z1_docker:/root/work \
  unitree-z1-noetic \
  bash
```

容器内：

```bash
cd /root/work
./setup_z1_noetic.sh
```

运行仿真：

```bash
# 终端 1
docker exec -it unitree-z1-noetic bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash
roslaunch unitree_gazebo z1.launch
```

```bash
# 终端 2
docker exec -it unitree-z1-noetic bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash
cd /root/work/unitree_z1/z1_controller/build
./sim_ctrl
```

```bash
# 终端 3
docker exec -it unitree-z1-noetic bash
cd /root/work/unitree_z1/z1_sdk/build
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/root/work/unitree_z1/z1_sdk/lib:/root/work/unitree_z1/z1_controller/lib
./highcmd_basic
```

---


git add README.md
git commit -m "Add Unitree Z1 Docker ROS1 simulation guide"
git push
```
