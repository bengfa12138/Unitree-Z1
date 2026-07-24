# Unitree Z1 Gazebo FT Sensor 闭环力控完整操作文档

本文档从你当前已经跑通的 Z1 Docker 仿真继续，目标是在现有 Gazebo Z1 机械臂上加入标准 force/torque sensor，并实现真正使用传感器反馈的闭环力控。

最终实现效果：

```text
Gazebo 末端力传感器发布 /z1/ft_sensor
控制程序订阅 /z1/ft_sensor
键盘设置目标力 F_desired
程序读取实际力 F_measured
计算 F_error = F_desired - F_measured
通过 LOWCMD 发送关节力矩 tau
```

本文档新增三个文件：

```text
z1_force_sensor_files/z1_ft_sensor_gazebo_patch.xacro
z1_force_sensor_files/z1_contact_block.sdf
z1_trajectory_examples/z1_closed_loop_force_control.cpp
```

其中：

```text
z1_ft_sensor_gazebo_patch.xacro
    用于给 Z1 Gazebo 模型增加 force/torque sensor。

z1_contact_block.sdf
    可选，用于在 Gazebo 中生成一个静态接触方块，让末端有东西可以压。

z1_closed_loop_force_control.cpp
    闭环力控程序，订阅 /z1/ft_sensor，并通过 LOWCMD 控制 Z1。
```

---

## 1. 闭环力控整体结构

你之前的实时力控是：

```text
键盘输入 Ftip
inverseDynamics 把 Ftip 转成 tau
LOWCMD 发给机械臂
```

这属于：

```text
开环力命令 / 虚拟外力前馈
```

本文档实现的是：

```text
键盘输入目标力 F_desired
Gazebo FT sensor 测量 F_measured
计算 F_error = F_desired - F_measured
根据 F_error 修正 commandWrench
inverseDynamics 把 commandWrench 转成 tau
LOWCMD 发给机械臂
```

代码中的核心公式是：

```cpp
Vec6 errorWrench = desiredWrench - measuredWrench;
commandWrench = desiredWrench;

for (int i = 0; i < 3; ++i)
{
    commandWrench(i) += momentFeedbackGain * errorWrench(i);
}

for (int i = 3; i < 6; ++i)
{
    commandWrench(i) += forceFeedbackGain * errorWrench(i);
}

arm.tau = arm._ctrlComp->armModel->inverseDynamics(
    arm.q,
    arm.qd,
    Vec6::Zero(),
    commandWrench
);
```

这就是一个简单的力闭环：

```text
测到的力小于目标力 -> error 为正 -> commandWrench 增大
测到的力大于目标力 -> error 变小或反向 -> commandWrench 减小
```

---

## 2. 先确认 Gazebo FT 插件是否存在

进入容器：

```bash
sudo docker exec -it unitree-z1-noetic bash
```

检查插件：

```bash
find /opt/ros/noetic -name "libgazebo_ros_ft_sensor.so"
```

正常应该能看到类似：

```text
/opt/ros/noetic/lib/libgazebo_ros_ft_sensor.so
```

如果没有，安装：

```bash
apt update
apt install -y ros-noetic-gazebo-ros-pkgs ros-noetic-gazebo-ros-control
```

如果容器内 `apt update` 因网络失败，可以先换清华源，或者沿用你前面 Docker 文档里的网络解决方法。

---

## 3. 找到 Z1 末端关节名

默认本文档使用：

```text
joint6
```

但为了保险，先检查你的模型：

```bash
grep -R "<joint name" /root/work/unitree_ws/src/unitree_ros/robots/z1_description
```

重点找类似：

```text
joint1
joint2
joint3
joint4
joint5
joint6
```

如果存在 `joint6`，后面直接用 `joint6`。

如果你的模型不是 `joint6`，就找最后一级腕部/末端关节，也就是通常连接：

```text
link05 -> link06
```

的那个关节。后面所有 `joint6` 都替换成你实际看到的关节名。

---

## 4. 给 Z1 加 force/torque sensor

打开 Z1 的 Gazebo xacro：

```bash
nano /root/work/unitree_ws/src/unitree_ros/robots/z1_description/xacro/gazebo.xacro
```

你应该能看到类似：

```xml
<robot xmlns:xacro="http://www.ros.org/wiki/xacro">

  <gazebo>
    <plugin name="gazebo_ros_control" filename="libgazebo_ros_control.so">
      <robotNamespace>/z1_gazebo</robotNamespace>
      <robotSimType>gazebo_ros_control/DefaultRobotHWSim</robotSimType>
    </plugin>
  </gazebo>

  ...

</robot>
```

在 `</robot>` 前面加入下面内容：

```xml
<!-- 让 Gazebo 对 joint6 计算关节力/力矩反馈。 -->
<gazebo reference="joint6">
  <provideFeedback>true</provideFeedback>
</gazebo>

<!-- Gazebo 标准 ROS force/torque 插件。 -->
<gazebo>
  <plugin name="z1_ft_sensor" filename="libgazebo_ros_ft_sensor.so">
    <updateRate>200.0</updateRate>
    <topicName>/z1/ft_sensor</topicName>
    <jointName>joint6</jointName>
  </plugin>
</gazebo>
```

如果你的末端关节名不是 `joint6`，这里两处都要替换：

```xml
<gazebo reference="你的末端关节名">
<jointName>你的末端关节名</jointName>
```

保存：

```text
Ctrl + O
Enter
Ctrl + X
```

本文档也提供了同样的片段文件：

```text
/root/work/z1_force_sensor_files/z1_ft_sensor_gazebo_patch.xacro
```

你可以用下面命令查看：

```bash
cat /root/work/z1_force_sensor_files/z1_ft_sensor_gazebo_patch.xacro
```

---

## 5. 重新启动 Gazebo 并确认传感器话题

因为修改了机器人模型，必须重启 Gazebo。

主机终端执行：

```bash
sudo docker restart unitree-z1-noetic
```

终端 1：

```bash
sudo docker exec -it unitree-z1-noetic bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash
roslaunch unitree_gazebo z1.launch
```

另开一个终端检查话题：

```bash
sudo docker exec -it unitree-z1-noetic bash
source /opt/ros/noetic/setup.bash
rostopic list | grep ft
```

正常应该看到：

```text
/z1/ft_sensor
```

继续查看数据：

```bash
rostopic echo /z1/ft_sensor
```

如果能看到 `force` 和 `torque` 字段，说明传感器已经生效：

```text
wrench:
  force:
    x: ...
    y: ...
    z: ...
  torque:
    x: ...
    y: ...
    z: ...
```

如果没有 `/z1/ft_sensor`，先看终端 1 的 Gazebo 启动输出，重点找：

```text
libgazebo_ros_ft_sensor.so
joint6
provideFeedback
```

常见原因：

```text
1. gazebo.xacro 没有保存
2. 加到了 </robot> 外面
3. joint6 名字不对
4. libgazebo_ros_ft_sensor.so 没安装
5. Gazebo 没有重启，仍然加载旧模型
```

---

## 6. 可选：生成一个接触方块

闭环力控最好让末端真的接触某个物体，否则传感器测到的接触力不会明显。

本文档提供了：

```text
z1_force_sensor_files/z1_contact_block.sdf
```

Gazebo 启动后，另开终端执行：

```bash
sudo docker exec -it unitree-z1-noetic bash
source /opt/ros/noetic/setup.bash
rosrun gazebo_ros spawn_model \
  -sdf \
  -file /root/work/z1_force_sensor_files/z1_contact_block.sdf \
  -model z1_contact_block \
  -x 0.50 -y 0.00 -z 0.55
```

如果方块位置不合适，可以删掉后重放：

```bash
rosservice call /gazebo/delete_model "model_name: 'z1_contact_block'"
```

然后换位置重新生成，例如：

```bash
rosrun gazebo_ros spawn_model \
  -sdf \
  -file /root/work/z1_force_sensor_files/z1_contact_block.sdf \
  -model z1_contact_block \
  -x 0.60 -y 0.00 -z 0.45
```

位置参数含义：

```text
-x 前后方向
-y 左右方向
-z 高度方向
```

不同 Gazebo 视角下你看到的方向可能不一样，以仿真画面为准。

---

## 7. 复制闭环控制代码

进入容器：

```bash
sudo docker exec -it unitree-z1-noetic bash
```

复制代码：

```bash
cp /root/work/z1_trajectory_examples/z1_closed_loop_force_control.cpp \
   /root/work/unitree_z1/z1_sdk/examples/z1_closed_loop_force_control.cpp
```

一行写法：

```bash
cp /root/work/z1_trajectory_examples/z1_closed_loop_force_control.cpp /root/work/unitree_z1/z1_sdk/examples/z1_closed_loop_force_control.cpp
```

检查：

```bash
ls -lh /root/work/unitree_z1/z1_sdk/examples/z1_closed_loop_force_control.cpp
```

---

## 8. 修改 z1_sdk 的 CMakeLists.txt

因为这次控制程序要订阅 ROS 话题，所以除了把 cpp 加到示例列表，还要让 `z1_sdk` 链接 ROS 的 `roscpp` 和 `geometry_msgs`。

进入 SDK 目录：

```bash
cd /root/work/unitree_z1/z1_sdk
```

打开：

```bash
nano CMakeLists.txt
```

### 8.1 加入 ROS 依赖

在 `project(...)` 后面，或者其他 `find_package(...)` 附近，加入：

```cmake
find_package(catkin REQUIRED COMPONENTS
    roscpp
    geometry_msgs
)
```

### 8.2 加入 ROS 头文件路径

找到 `include_directories(...)`，把 `${catkin_INCLUDE_DIRS}` 加进去。

例如原来可能是：

```cmake
include_directories(
    include
)
```

改成：

```cmake
include_directories(
    include
    ${catkin_INCLUDE_DIRS}
)
```

如果你的 `include_directories` 里面还有其他路径，不要删，直接追加 `${catkin_INCLUDE_DIRS}`。

### 8.3 加入新的示例文件

找到：

```cmake
set(EXAMPLES_FILES
    examples/highcmd_basic.cpp
    examples/highcmd_development.cpp
    examples/lowcmd_development.cpp
    examples/lowcmd_multirobots.cpp
)
```

加入：

```cmake
    examples/z1_closed_loop_force_control.cpp
```

如果你之前已经加过其他示例，保留即可。例如：

```cmake
set(EXAMPLES_FILES
    examples/highcmd_basic.cpp
    examples/highcmd_development.cpp
    examples/lowcmd_development.cpp
    examples/lowcmd_multirobots.cpp
    examples/z1_custom_waypoints.cpp
    examples/z1_keyboard_control.cpp
    examples/z1_force_control.cpp
    examples/z1_keyboard_force_control.cpp
    examples/z1_closed_loop_force_control.cpp
)
```

### 8.4 给闭环程序链接 ROS 库

在 CMakeLists.txt 靠后位置，所有示例 `add_executable` 创建完成之后，加入：

```cmake
if(TARGET z1_closed_loop_force_control)
    target_link_libraries(z1_closed_loop_force_control ${catkin_LIBRARIES})
    add_dependencies(z1_closed_loop_force_control ${catkin_EXPORTED_TARGETS})
endif()
```

如果你的 CMakeLists.txt 是在循环里统一 `target_link_libraries`，也可以把 `${catkin_LIBRARIES}` 直接加到统一链接列表中。

保存：

```text
Ctrl + O
Enter
Ctrl + X
```

确认：

```bash
grep -n "catkin\\|z1_closed_loop_force_control" CMakeLists.txt
```

能看到 `catkin` 和 `z1_closed_loop_force_control` 就可以。

---

## 9. 编译闭环控制程序

一定要在容器路径编译：

```bash
cd /root/work/unitree_z1/z1_sdk
rm -rf build
mkdir build
cd build
source /opt/ros/noetic/setup.bash
cmake ..
make -j$(nproc)
```

检查可执行文件：

```bash
ls -lh z1_closed_loop_force_control
```

如果能看到：

```text
-rwxr-xr-x ... z1_closed_loop_force_control
```

说明编译成功。

---

## 10. 三终端运行完整闭环力控

### 10.1 终端 1：启动 Gazebo

```bash
sudo docker restart unitree-z1-noetic
sudo docker exec -it unitree-z1-noetic bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash
roslaunch unitree_gazebo z1.launch
```

### 10.2 终端 2：启动 sim_ctrl

```bash
sudo docker exec -it unitree-z1-noetic bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash
cd /root/work/unitree_z1/z1_controller/build
./sim_ctrl
```

### 10.3 终端 3：运行闭环力控程序

```bash
sudo docker exec -it unitree-z1-noetic bash
source /opt/ros/noetic/setup.bash
cd /root/work/unitree_z1/z1_sdk/build
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/root/work/unitree_z1/z1_sdk/lib:/root/work/unitree_z1/z1_controller/lib
./z1_closed_loop_force_control
```

程序启动后会：

```text
1. 订阅 /z1/ft_sensor
2. 回到 SDK 起始姿态
3. 切换 LOWCMD
4. 移动到观察姿态
5. 等待 FT sensor 数据
6. 进入键盘闭环力控循环
```

---

## 11. 按键说明

按键要在终端 3 中输入。

### 11.1 设置目标线性力

```text
W / S : Fx + / -
A / D : Fy + / -
R / F : Fz + / -
```

每按一次改变：

```cpp
const double forceStep = 0.50;
```

目标力最大限制：

```cpp
const double desiredForceLimit = 8.00;
```

### 11.2 设置目标转动力矩

```text
Q / E : Mz + / -
```

每按一次改变：

```cpp
const double momentStep = 0.08;
```

目标力矩最大限制：

```cpp
const double desiredMomentLimit = 1.00;
```

### 11.3 传感器和闭环控制

```text
B : 以当前 FT sensor 读数为零点 tare
I : 反转 measuredWrench 符号
C : 切换柔顺档位 very_soft / soft / normal
0 : 清零目标力和命令力
P : 打印当前状态
H : 显示帮助
Esc : 退出程序
```

其中 `B` 很重要。建议每次程序进入力控循环后，先在没有明显接触力的情况下按一次：

```text
B
```

这会把当前读数当作零点，减少重力、模型初始载荷或传感器偏置对闭环控制的影响。

---

## 12. 推荐第一次实验步骤

第一次不要急着给很大的力，按下面顺序：

```text
1. 启动 Gazebo，确认 /z1/ft_sensor 存在
2. 启动 sim_ctrl
3. 启动 ./z1_closed_loop_force_control
4. 等程序进入闭环循环
5. 按 B 标定零点
6. 可选：生成 z1_contact_block
7. 让末端靠近或接触方块
8. 按 P 看 measured 是否接近 0
9. 按 C 切到 very_soft
10. 连续按 R 两三次，设置一个小的 Fz 目标力
11. 观察 measuredWrench 是否变化
12. 如果 measured 与 desired 方向相反，按 I 反转传感器符号
13. 按 0 清零
14. 按 Esc 退出
```

判断闭环是否在工作，看终端 3 中 `P` 打印的内容：

```text
desired  = 你的目标力
measured = 传感器实际测得力
command  = 闭环控制器实际发给 inverseDynamics 的命令力
```

如果 `measured` 比 `desired` 小，`command` 通常会比 `desired` 大一些。

如果 `measured` 比 `desired` 大，`command` 会减小。

这说明闭环反馈正在参与控制。

---

## 13. 如果方向不对怎么办

Gazebo FT sensor 的方向不一定和你在 `inverseDynamics(..., commandWrench)` 里使用的方向一致。

典型表现：

```text
你希望 Fz 增大
但 measured Fz 变成负数
或者 command 越修正越大，机械臂越压越狠
```

这时候按：

```text
I
```

程序会切换：

```text
sensorSign = 1
sensorSign = -1
```

也就是：

```cpp
Vec6 measuredWrench = sensorSign * (rawWrench - tareWrench);
```

切换后再按：

```text
P
```

观察 `desired`、`measured`、`command` 是否更符合直觉。

---

## 14. 调参位置

闭环控制参数在：

```text
z1_closed_loop_force_control.cpp
```

### 14.1 目标力每次按键步长

```cpp
const double forceStep = 0.50;
const double momentStep = 0.08;
```

如果效果太弱：

```cpp
const double forceStep = 0.80;
```

如果机械臂动作太猛：

```cpp
const double forceStep = 0.25;
```

### 14.2 目标力限制

```cpp
const double desiredForceLimit = 8.00;
const double desiredMomentLimit = 1.00;
```

第一次实验不建议超过：

```text
8N
```

### 14.3 命令力限制

```cpp
const double commandForceLimit = 12.00;
const double commandMomentLimit = 1.50;
```

这两个值是保护用的。如果闭环增益较大，`commandWrench` 会被限制在这个范围内。

### 14.4 闭环反馈增益

```cpp
const double forceFeedbackGain = 0.70;
const double momentFeedbackGain = 0.45;
```

如果闭环太弱，`measured` 很难靠近 `desired`：

```cpp
const double forceFeedbackGain = 1.00;
```

如果闭环抖动或过冲明显：

```cpp
const double forceFeedbackGain = 0.40;
```

### 14.5 柔顺档位

```cpp
if (level == 0)
{
    stiffnessScale = 0.22;
    dampingScale = 0.50;
}
else if (level == 1)
{
    stiffnessScale = 0.35;
    dampingScale = 0.65;
}
else
{
    stiffnessScale = 0.55;
    dampingScale = 0.85;
}
```

越软，越容易看到力控效果，但越容易抖。

---

## 15. 常见问题

### 15.1 rostopic list 看不到 /z1/ft_sensor

检查：

```bash
find /opt/ros/noetic -name "libgazebo_ros_ft_sensor.so"
grep -n "z1_ft_sensor\\|joint6\\|provideFeedback" /root/work/unitree_ws/src/unitree_ros/robots/z1_description/xacro/gazebo.xacro
grep -R "<joint name" /root/work/unitree_ws/src/unitree_ros/robots/z1_description
```

常见原因：

```text
1. 插件没安装
2. xacro 没保存
3. joint6 名字不对
4. 修改后没有重启 Gazebo
5. XML 加到了 </robot> 外面
```

### 15.2 rostopic echo 有数据，但程序说没收到 FT sensor

确认程序订阅的是：

```text
/z1/ft_sensor
```

如果你改过话题名，可以运行时传 ROS 参数：

```bash
./z1_closed_loop_force_control _ft_topic:=/你的话题名
```

### 15.3 编译时报 ros/ros.h 找不到

说明 CMakeLists 没有加：

```cmake
find_package(catkin REQUIRED COMPONENTS
    roscpp
    geometry_msgs
)
```

或者 `include_directories` 没有加：

```cmake
${catkin_INCLUDE_DIRS}
```

也要确认编译前执行过：

```bash
source /opt/ros/noetic/setup.bash
```

### 15.4 编译时报 undefined reference to ros...

说明目标没有链接 ROS 库。

在 CMakeLists.txt 后面加：

```cmake
if(TARGET z1_closed_loop_force_control)
    target_link_libraries(z1_closed_loop_force_control ${catkin_LIBRARIES})
    add_dependencies(z1_closed_loop_force_control ${catkin_EXPORTED_TARGETS})
endif()
```

然后重新：

```bash
cd /root/work/unitree_z1/z1_sdk
rm -rf build
mkdir build
cd build
source /opt/ros/noetic/setup.bash
cmake ..
make -j$(nproc)
```

### 15.5 measuredWrench 数值很大，一开始就不为 0

这很常见，因为 FT sensor 可能测到了模型载荷、关节内部反力或初始偏置。

进入闭环后先按：

```text
B
```

把当前读数作为零点。

### 15.6 一给目标力机械臂就越来越用力

通常是传感器方向反了。

按：

```text
I
```

然后按：

```text
P
```

观察 `measured` 是否和 `desired` 方向一致。

### 15.7 机械臂抖动

先按：

```text
0
```

清零目标力。

然后：

```text
C
```

切到更硬的档位。

如果还抖，改小：

```cpp
const double forceFeedbackGain = 0.40;
const double forceStep = 0.25;
```

---

## 16. 上传 GitHub 建议

建议上传这些文件：

```text
Z1_CLOSED_LOOP_FORCE_CONTROL_GUIDE.md
z1_force_sensor_files/z1_ft_sensor_gazebo_patch.xacro
z1_force_sensor_files/z1_contact_block.sdf
z1_trajectory_examples/z1_closed_loop_force_control.cpp
```

如果要从 README 直接跳转，也可以在 README 的扩展文档里加入：

```markdown
- [Z1_CLOSED_LOOP_FORCE_CONTROL_GUIDE.md](Z1_CLOSED_LOOP_FORCE_CONTROL_GUIDE.md)：Gazebo 标准 force/torque sensor + LOWCMD 的闭环力控。
```
