# Unitree Z1 键盘实时力控操作文档

本文档从你已经跑通的 Z1 Gazebo 仿真继续，不重新讲 Docker、ROS1 Noetic、Gazebo 的完整安装过程。

目标是实现：

```text
在 Gazebo 仿真运行时，通过键盘实时给 Z1 机械臂施加末端空间力或关节力矩，
让机械臂出现明显的柔顺、受力偏移、被推/被拉的效果。
```

本文档提供的代码文件是：

```text
z1_trajectory_examples/z1_keyboard_force_control.cpp
```

复制到 SDK 后，对应容器内路径是：

```text
/root/work/unitree_z1/z1_sdk/examples/z1_keyboard_force_control.cpp
```

编译后生成的程序是：

```text
/root/work/unitree_z1/z1_sdk/build/z1_keyboard_force_control
```

---

## 1. 先说明：这里的“完整力控”是什么

你现在的 Z1 仿真结构大概是：

```text
z1_sdk 示例程序
        |
        |  UDP / SDK 命令
        v
z1_controller/build/sim_ctrl
        |
        |  ROS / Gazebo 控制接口
        v
Gazebo 里的 Z1 机械臂
```

要想让 Z1 在仿真中表现出力控效果，最直接的方法是使用 SDK 的 `LOWCMD`：

```cpp
arm.setFsm(ArmFSMState::LOWCMD);
arm.setArmCmd(arm.q, arm.qd, arm.tau);
```

其中：

```text
q   = 目标关节角
qd  = 目标关节速度
tau = 关节力矩前馈
```

本示例的核心控制公式是：

```cpp
arm.tau = arm._ctrlComp->armModel->inverseDynamics(
    arm.q,
    arm.qd,
    Vec6::Zero(),
    ftip
) + tauBias;
```

含义是：

```text
ftip    ：你希望施加到末端的 6 维空间力/力矩
tauBias ：你希望额外施加到各个关节的力矩
inverseDynamics(...)：把末端空间力换算成关节力矩
```

所以，键盘按键不是直接拖动 Gazebo 模型，而是实时改变 `ftip` 和 `tauBias`，程序再把这些力转换成 Z1 的底层力矩命令。

---

## 2. 它和真实闭环接触力控的区别

严格意义上的闭环接触力控需要测量实际接触力：

```text
F_desired  = 目标接触力
F_measured = 实际测量接触力
F_error    = F_desired - F_measured
```

然后根据 `F_error` 调整机械臂命令。

例如：

```text
如果希望末端压住桌面 5N：
目标力是 5N
传感器测到 3N
误差是 2N
控制器继续让机械臂往下压
```

而本文档这个版本是：

```text
键盘实时给一个虚拟末端力命令 ftip，
让仿真机械臂表现出明显的受力和柔顺效果。
```

它已经比之前的 `z1_force_control.cpp` 更接近你要的“实时力控实验”，但还不是带力传感器反馈的闭环接触力控制。

如果后面要做真正闭环，需要继续加：

```text
Gazebo force_torque sensor
或 Gazebo contact sensor
或真实机械臂腕部六维力传感器
```

然后写 ROS 节点读取力传感器话题。

---

## 3. 准备代码文件

本仓库已经提供：

```text
z1_trajectory_examples/z1_keyboard_force_control.cpp
```

如果你是在主机上操作，并且你的工程目录是：

```text
~/unitree_z1_docker
```

那么这个文件应该放在主机：

```text
~/unitree_z1_docker/z1_trajectory_examples/z1_keyboard_force_control.cpp
```

进入 Docker 容器后，对应路径就是：

```text
/root/work/z1_trajectory_examples/z1_keyboard_force_control.cpp
```

进入容器：

```bash
sudo docker exec -it unitree-z1-noetic bash
```

如果你已经把当前用户加入了 `docker` 用户组，也可以不用 `sudo`：

```bash
docker exec -it unitree-z1-noetic bash
```

检查文件是否存在：

```bash
ls -lh /root/work/z1_trajectory_examples/z1_keyboard_force_control.cpp
```

能看到文件大小，就说明准备好了。

---

## 4. 复制代码到 z1_sdk/examples

在容器中执行：

```bash
cp /root/work/z1_trajectory_examples/z1_keyboard_force_control.cpp \
   /root/work/unitree_z1/z1_sdk/examples/z1_keyboard_force_control.cpp
```

一行写法是：

```bash
cp /root/work/z1_trajectory_examples/z1_keyboard_force_control.cpp /root/work/unitree_z1/z1_sdk/examples/z1_keyboard_force_control.cpp
```

检查是否复制成功：

```bash
ls -lh /root/work/unitree_z1/z1_sdk/examples/z1_keyboard_force_control.cpp
```

---

## 5. 修改 CMakeLists.txt

进入 SDK 目录：

```bash
cd /root/work/unitree_z1/z1_sdk
```

编辑 CMakeLists：

```bash
nano CMakeLists.txt
```

找到类似内容：

```cmake
set(EXAMPLES_FILES
    examples/highcmd_basic.cpp
    examples/highcmd_development.cpp
    examples/lowcmd_development.cpp
    examples/lowcmd_multirobots.cpp
)
```

加入这一行：

```cmake
    examples/z1_keyboard_force_control.cpp
```

修改后应该类似：

```cmake
set(EXAMPLES_FILES
    examples/highcmd_basic.cpp
    examples/highcmd_development.cpp
    examples/lowcmd_development.cpp
    examples/lowcmd_multirobots.cpp
    examples/z1_keyboard_force_control.cpp
)
```

如果你之前还添加过其他示例，比如：

```cmake
    examples/z1_custom_waypoints.cpp
    examples/z1_keyboard_control.cpp
    examples/z1_force_control.cpp
```

那也可以保留，不需要删除。最终可以是：

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
)
```

保存：

```text
Ctrl + O
Enter
Ctrl + X
```

确认已经添加：

```bash
grep -n "z1_keyboard_force_control" CMakeLists.txt
```

如果能看到：

```text
examples/z1_keyboard_force_control.cpp
```

说明 CMake 已经改好。

---

## 6. 清理旧 build，避免 CMakeCache 路径错误

你之前遇到过这种错误：

```text
CMake Error: The current CMakeCache.txt directory ...
is different than the directory /root/work/... where CMakeCache.txt was created.
```

所以建议这次直接在容器里清理旧 build。

必须先进到容器路径：

```bash
cd /root/work/unitree_z1/z1_sdk
pwd
```

输出必须是：

```text
/root/work/unitree_z1/z1_sdk
```

然后执行：

```bash
rm -rf build
mkdir build
cd build
```

注意：

```text
rm -rf build 只删除编译缓存目录，不删除源码。
不要在错误目录下执行 rm -rf。
```

---

## 7. 编译

在容器内执行：

```bash
cd /root/work/unitree_z1/z1_sdk/build
cmake ..
make -j$(nproc)
```

编译完成后检查：

```bash
ls -lh z1_keyboard_force_control
```

如果看到：

```text
-rwxr-xr-x ... z1_keyboard_force_control
```

说明编译成功。

---

## 8. 运行仿真

需要三个终端。

### 8.1 终端 1：启动 Gazebo

主机打开终端 1：

```bash
sudo docker restart unitree-z1-noetic
sudo docker exec -it unitree-z1-noetic bash
```

进入容器后：

```bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash
roslaunch unitree_gazebo z1.launch
```

等待 Gazebo 打开，并看到 Z1 机械臂。

如果 Gazebo GUI 打不开，先在主机执行：

```bash
xhost +local:root
```

然后重新启动容器和 Gazebo。

### 8.2 终端 2：启动 sim_ctrl

主机打开终端 2：

```bash
sudo docker exec -it unitree-z1-noetic bash
```

进入容器后：

```bash
source /opt/ros/noetic/setup.bash
source /root/work/unitree_ws/devel/setup.bash
cd /root/work/unitree_z1/z1_controller/build
./sim_ctrl
```

如果看到：

```text
connect with z1_sdk wait time out
Lose connection with z1_sdk
```

不用慌，这是 `sim_ctrl` 在等终端 3 的 SDK 程序连接。

### 8.3 终端 3：运行键盘实时力控程序

主机打开终端 3：

```bash
sudo docker exec -it unitree-z1-noetic bash
```

进入容器后：

```bash
cd /root/work/unitree_z1/z1_sdk/build
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/root/work/unitree_z1/z1_sdk/lib:/root/work/unitree_z1/z1_controller/lib
./z1_keyboard_force_control
```

注意：

```text
键盘按键要在终端 3 里输入，不是在 Gazebo 窗口里输入。
```

---

## 9. 按键说明

程序启动后会显示帮助信息。

### 9.1 末端线性力

这些键用于改变 `Ftip` 的后三项，也就是末端线性力：

```text
W / S : Ftip X 方向 + / -
A / D : Ftip Y 方向 + / -
R / F : Ftip Z 方向 + / -
```

每按一次，力会增加或减少：

```cpp
const double forceStep = 0.15;
```

默认最大限制是：

```cpp
const double forceLimit = 2.00;
```

### 9.2 末端转动力矩

这些键用于改变 `Ftip` 的前三项中的一个：

```text
Q / E : Ftip 绕 Z 方向 + / -
```

每按一次，末端转动力矩会增加或减少：

```cpp
const double momentStep = 0.04;
```

默认最大限制是：

```cpp
const double momentLimit = 0.60;
```

### 9.3 关节力矩前馈

先选择关节：

```text
1~6 : 选择第几个关节
```

然后调力矩：

```text
M / N : 当前关节力矩 + / -
```

每按一次，关节力矩前馈会增加或减少：

```cpp
const double jointTorqueStep = 0.015;
```

默认最大限制是：

```cpp
const double jointTorqueLimit = 0.15;
```

### 9.4 清零和退出

```text
0   : 清零 Ftip 和 tauBias
P   : 打印当前状态
H   : 显示帮助
Esc : 退出程序
```

---

## 10. 怎么让效果更明显

### 10.1 切到更软的档位

运行时按：

```text
C
```

程序会在三个档位之间切换：

```text
very_soft
soft
normal
```

其中：

```text
very_soft 最软，受力偏移最明显，但也最容易抖。
normal 最硬，最稳定，但力控效果不那么明显。
```

代码位置：

```cpp
if (level == 0)
{
    stiffnessScale = 0.20;
    dampingScale = 0.45;
    name = "very_soft";
}
else if (level == 1)
{
    stiffnessScale = 0.35;
    dampingScale = 0.65;
    name = "soft";
}
else
{
    stiffnessScale = 0.55;
    dampingScale = 0.85;
    name = "normal";
}
```

如果你觉得效果不明显，可以把 `very_soft` 再调软一点：

```cpp
stiffnessScale = 0.15;
dampingScale = 0.40;
```

但如果出现明显抖动，就把它调回：

```cpp
stiffnessScale = 0.25;
dampingScale = 0.55;
```

### 10.2 增大每次按键施加的力

代码位置：

```cpp
const double forceStep = 0.15;
const double momentStep = 0.04;
const double jointTorqueStep = 0.015;
```

如果效果太小，可以改成：

```cpp
const double forceStep = 0.25;
const double momentStep = 0.06;
const double jointTorqueStep = 0.025;
```

### 10.3 增大最大限制

代码位置：

```cpp
const double forceLimit = 2.00;
const double momentLimit = 0.60;
const double jointTorqueLimit = 0.15;
```

如果已经按了很多次，程序显示的 `Ftip` 到了限制值，但 Gazebo 里还是不明显，可以逐步改成：

```cpp
const double forceLimit = 3.00;
const double momentLimit = 0.80;
const double jointTorqueLimit = 0.20;
```

不要一开始就改得很大。先在仿真里一点一点试。

---

## 11. 推荐第一次测试方法

第一次运行时，按下面顺序试：

```text
1. 等 Z1 到达观察姿态
2. 按 P，看当前 Ftip 和 tauBias 是否全为 0
3. 按 C，切到 very_soft
4. 连续按 R 三到五次，观察末端是否在某个方向有明显偏移
5. 按 F 三到五次，观察末端是否反向变化
6. 按 0 清零，观察机械臂是否回到保持姿态附近
7. 按 2，选择第 2 关节
8. 连续按 M 三到五次，观察第 2 关节是否有明显受力趋势
9. 按 N 三到五次，观察是否反向
10. 按 Esc 退出
```

如果方向和你直觉不一致，不一定是代码错了。`Ftip` 所在坐标系和 Gazebo 视角不完全等同，你应该以 Gazebo 里的实际响应方向为准。

---

## 12. 常见问题

### 12.1 程序运行了，但按键没反应

先确认你按键的位置：

```text
要在运行 ./z1_keyboard_force_control 的终端 3 中按键，
不是在 Gazebo 窗口中按键。
```

然后确认终端 3 不是卡在别的命令里：

```text
如果程序正常运行，终端会显示帮助信息和当前状态。
```

### 12.2 机械臂动得太小

按：

```text
C
```

切到 `very_soft`。

然后多按几次：

```text
R 或 F
W 或 S
A 或 D
```

如果还是太小，再修改代码中的：

```cpp
const double forceStep = 0.15;
const double forceLimit = 2.00;
```

例如：

```cpp
const double forceStep = 0.25;
const double forceLimit = 3.00;
```

重新编译运行。

### 12.3 机械臂抖动明显

说明太软或者力太大。

先按：

```text
0
```

清零所有力。

再按：

```text
C
```

切到更硬的档位，比如 `soft` 或 `normal`。

如果还是抖，改小：

```cpp
const double forceStep = 0.10;
const double forceLimit = 1.50;
```

或者把 `very_soft` 改硬一点：

```cpp
stiffnessScale = 0.25;
dampingScale = 0.55;
```

### 12.4 按 Esc 退出后终端输入不显示

正常情况下代码会自动恢复终端。如果异常退出导致终端不显示，可以在当前终端输入：

```bash
reset
```

然后回车。

### 12.5 编译后找不到 z1_keyboard_force_control

检查 CMakeLists：

```bash
cd /root/work/unitree_z1/z1_sdk
grep -n "z1_keyboard_force_control" CMakeLists.txt
```

如果没有输出，说明还没有添加：

```cmake
examples/z1_keyboard_force_control.cpp
```

添加后重新编译：

```bash
cd /root/work/unitree_z1/z1_sdk
rm -rf build
mkdir build
cd build
cmake ..
make -j$(nproc)
```

---

## 13. 如果你要做真正的接触力闭环

当前示例实现的是：

```text
键盘给目标外力 Ftip
SDK 模型把 Ftip 换算成关节力矩
LOWCMD 发送 q / qd / tau
Gazebo 中看到柔顺受力效果
```

真正闭环接触力控还需要：

```text
1. 在 Gazebo 里给末端或夹爪加 force_torque / contact sensor
2. 让 Gazebo 发布实际接触力 F_measured
3. 写一个 ROS 节点订阅 F_measured
4. 用 F_desired - F_measured 得到力误差
5. 把力误差转换成末端速度、末端位移或关节力矩
6. 再通过 LOWCMD 发给 Z1
```

典型导纳控制结构：

```text
F_error = F_desired - F_measured
x_dot_cmd = K_force * F_error
q_cmd = q_cmd + J(q)^-1 * x_dot_cmd * dt
```

典型阻抗控制结构：

```text
F_cmd = Kx * (x_desired - x) + Dx * (xdot_desired - xdot)
tau = J(q)^T * F_cmd + gravity_compensation
```

你现在应该先把 `z1_keyboard_force_control` 跑通。它能帮你确认三件事：

```text
1. LOWCMD 能稳定运行
2. tau 前馈能影响仿真机械臂
3. 降低刚度后，Gazebo 中能看到明显柔顺效果
```

这三件事确认后，再继续加 Gazebo 力传感器，才是真正闭环力控。

---

## 14. 完整代码

完整代码已经保存为：

```text
z1_trajectory_examples/z1_keyboard_force_control.cpp
```

上传 GitHub 时建议同时上传：

```text
Z1_KEYBOARD_FORCE_CONTROL_GUIDE.md
z1_trajectory_examples/z1_keyboard_force_control.cpp
```

