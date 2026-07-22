# Unitree Z1 仿真力控操作文档

本文档从当前已经跑通的 Z1 Docker 仿真环境继续。

当前默认已经具备：

```text
Ubuntu 22.04 / 24.04 主机
Docker 容器 unitree-z1-noetic
容器内 ROS1 Noetic
Gazebo 中可以启动 Z1
sim_ctrl 可以运行
z1_sdk 示例可以编译运行
```

目标：

```text
在现有 Z1 Gazebo 仿真基础上，引入 LOWCMD 底层控制，
通过 q / qd / tau 控制方式实现力控入门实验。
```

本文档会生成并使用：

```text
z1_force_control.cpp
```

它实现：

```text
1. 切换到 LOWCMD
2. 降低 Kp/Kd，让机械臂变柔顺
3. 保持一个安全姿态
4. 加入小的关节力矩前馈 tauBias
5. 加入小的末端空间力前馈 Ftip
```

> 重要说明：本文档中的“力控”是力控入门实验，属于 LOWCMD 力矩前馈和柔顺控制。它还不是完整的接触力闭环控制。真正的闭环力控还需要 Gazebo 力传感器、接触传感器或真实力传感器反馈。

---

## 1. 先理解：为什么要用 LOWCMD？

你之前用过的这些程序：

```text
highcmd_basic
highcmd_development
z1_custom_waypoints
z1_keyboard_control
```

主要是：

```text
关节位置 / 速度控制
```

核心形式是：

```cpp
arm.q = targetQ;
arm.qd = targetQd;
arm.setArmCmd(arm.q, arm.qd);
```

如果要引入力控，需要能够给每个关节发送力矩前馈：

```cpp
arm.tau = targetTau;
arm.setArmCmd(arm.q, arm.qd, arm.tau);
```

这就需要切换到：

```cpp
ArmFSMState::LOWCMD
```

而不是：

```cpp
ArmFSMState::JOINTCTRL
```

所以力控相关代码应该从官方：

```text
/root/work/unitree_z1/z1_sdk/examples/lowcmd_development.cpp
```

这一类示例开始，而不是继续改 `highcmd_basic.cpp`。

---

## 2. 力控分几种？

本文档按从简单到复杂分成三层。

### 2.1 低刚度柔顺控制

降低底层控制器的：

```text
Kp
Kd
```

让机械臂不再非常硬地追踪目标位置，而是更柔顺。

这一步本质是：

```text
位置控制仍然存在，但刚度变低
```

优点：

```text
最容易在当前仿真里实现
风险相对低
能明显感受到机械臂变软
```

### 2.2 关节力矩前馈

给某个关节额外加一个小力矩：

```cpp
tauBias(1) = 0.03;
```

然后叠加到模型补偿力矩上：

```cpp
arm.tau = tauModel + tauBias;
```

这一步可以理解为：

```text
额外给某个关节推一把力
```

### 2.3 末端空间力前馈

给末端一个空间力：

```cpp
Vec6 ftip;
ftip << 0.0, 0.0, 0.0, 0.0, 0.0, 0.30;
```

再通过 SDK 的动力学模型转换成关节力矩：

```cpp
arm.tau = arm._ctrlComp->armModel->inverseDynamics(
    arm.q,
    arm.qd,
    Vec6::Zero(),
    ftip
);
```

这一步可以理解为：

```text
希望末端产生某个方向的力，SDK 模型帮你换算成各关节力矩
```

### 2.4 真正的闭环力控

真正的力控需要：

```text
目标力 F_des
实际测量力 F_meas
力误差 F_err = F_des - F_meas
根据 F_err 调整 q / qd / tau
```

你的当前仿真默认不一定有末端力传感器，所以本文档先做：

```text
LOWCMD + 柔顺控制 + 力矩前馈
```

之后如果要做闭环力控，再加 Gazebo force_torque sensor 或 contact sensor。

---

## 3. 本文档使用的路径规则

你的主机路径可能是：

```text
/home/boat/unitree_z1_docker/...
```

而 Docker 容器内路径是：

```text
/root/work/...
```

因为启动容器时一般用了挂载：

```bash
-v ~/unitree_z1_docker:/root/work
```

所以：

```text
主机 /home/boat/unitree_z1_docker
等价于
容器 /root/work
```

后面本文档仍然统一使用容器路径：

```text
/root/work/...
```

推荐规则：

```text
可以在主机编辑代码
但编译和运行一定进入 Docker 容器，用 /root/work/... 路径
```

否则容易出现 CMakeCache 路径冲突：

```text
CMakeCache.txt directory ... is different than ...
```

---

## 4. 准备代码文件

本文档已经提供完整代码文件：

```text
z1_trajectory_examples/z1_force_control.cpp
```

如果你是在主机上操作，请把这个文件放到：

```text
~/unitree_z1_docker/z1_trajectory_examples/z1_force_control.cpp
```

这样 Docker 容器里会看到：

```text
/root/work/z1_trajectory_examples/z1_force_control.cpp
```

进入容器：

```bash
sudo docker exec -it unitree-z1-noetic bash
```

如果你不需要 `sudo`，也可以：

```bash
docker exec -it unitree-z1-noetic bash
```

进入后确认文件存在：

```bash
ls -lh /root/work/z1_trajectory_examples/z1_force_control.cpp
```

如果能看到文件，继续下一步。

---

## 5. 复制力控代码到 z1_sdk 示例目录

进入容器后执行：

```bash
cp /root/work/z1_trajectory_examples/z1_force_control.cpp \
   /root/work/unitree_z1/z1_sdk/examples/z1_force_control.cpp
```

一行写法：

```bash
cp /root/work/z1_trajectory_examples/z1_force_control.cpp /root/work/unitree_z1/z1_sdk/examples/z1_force_control.cpp
```

检查是否复制成功：

```bash
ls -lh /root/work/unitree_z1/z1_sdk/examples/z1_force_control.cpp
```

---

## 6. 修改 z1_sdk 的 CMakeLists.txt

进入 SDK 目录：

```bash
cd /root/work/unitree_z1/z1_sdk
```

编辑：

```bash
nano CMakeLists.txt
```

找到类似这段：

```cmake
set(EXAMPLES_FILES
    examples/highcmd_basic.cpp
    examples/highcmd_development.cpp
    examples/lowcmd_development.cpp
    examples/lowcmd_multirobots.cpp
)
```

加入一行：

```cmake
    examples/z1_force_control.cpp
```

修改后应类似：

```cmake
set(EXAMPLES_FILES
    examples/highcmd_basic.cpp
    examples/highcmd_development.cpp
    examples/lowcmd_development.cpp
    examples/lowcmd_multirobots.cpp
    examples/z1_force_control.cpp
)
```

保存：

```text
Ctrl + O
Enter
Ctrl + X
```

检查是否添加成功：

```bash
grep -n "z1_force_control" CMakeLists.txt
```

如果输出：

```text
examples/z1_force_control.cpp
```

说明 CMakeLists.txt 已经改好。

---

## 7. 清理旧的 CMake 缓存

如果你之前在主机路径和容器路径之间混用过 `cmake ..`，建议先清理 `build` 目录。

必须先确认你在容器路径：

```bash
cd /root/work/unitree_z1/z1_sdk
pwd
```

输出应为：

```text
/root/work/unitree_z1/z1_sdk
```

然后删除旧 build：

```bash
rm -rf build
```

重新创建：

```bash
mkdir build
cd build
```

> 注意：`rm -rf build` 只删除 CMake 编译目录，不会删除源码。不要在错误目录执行这个命令。

---

## 8. 编译 z1_force_control

在容器内执行：

```bash
cd /root/work/unitree_z1/z1_sdk/build
cmake ..
make -j$(nproc)
```

编译完成后检查可执行文件：

```bash
ls -lh z1_force_control
```

如果能看到类似：

```text
-rwxr-xr-x ... z1_force_control
```

说明编译成功。

---

## 9. 运行力控仿真

需要三个终端。

### 9.1 终端 1：启动 Gazebo

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

等待 Gazebo 打开，并看到 Z1 机械臂模型。

### 9.2 终端 2：启动 sim_ctrl

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

这是正常的，说明 `sim_ctrl` 正在等终端 3 的 SDK 程序连接。

### 9.3 终端 3：运行力控程序

主机打开终端 3：

```bash
sudo docker exec -it unitree-z1-noetic bash
```

进入容器后：

```bash
cd /root/work/unitree_z1/z1_sdk/build
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/root/work/unitree_z1/z1_sdk/lib:/root/work/unitree_z1/z1_controller/lib
./z1_force_control
```

运行后，程序会依次打印：

```text
[1/6] 回到 SDK 起始姿态...
[2/6] 切换到 PASSIVE，再切换到 LOWCMD...
[3/6] LOWCMD 运动到观察姿态...
[4/6] 保持观察姿态 2 秒，只使用模型补偿...
[5/6] 加入小的关节力矩前馈，观察机械臂柔顺响应...
[6/6] 加入很小的末端空间力前馈 Ftip，观察响应方向...
```

你需要观察 Gazebo 中 Z1 在第 5 步和第 6 步是否出现轻微柔顺响应。

---

## 10. 如何调参？

力控相关的主要参数在 `z1_force_control.cpp` 中。

### 10.1 调整柔顺程度

代码位置：

```cpp
const double stiffnessScale = 0.45;
const double dampingScale = 0.70;
```

含义：

```text
stiffnessScale 越小，位置刚度越低，机械臂越软
dampingScale 太小可能抖动，太大动作会钝
```

建议范围：

```text
stiffnessScale: 0.30 ~ 0.80
dampingScale:   0.50 ~ 1.00
```

第一次不要改得太激进。

### 10.2 调整关节力矩前馈

代码位置：

```cpp
Vec6 tauBias = Vec6::Zero();
tauBias(1) = 0.03;
```

含义：

```text
tauBias(1) 表示给第 2 个关节加一个小的额外力矩
```

如果效果太小，可以逐步试：

```cpp
tauBias(1) = 0.04;
tauBias(1) = 0.06;
```

不要一开始给很大力矩。

### 10.3 调整末端空间力前馈

代码位置：

```cpp
Vec6 ftip = Vec6::Zero();
ftip << 0.0, 0.0, 0.0, 0.0, 0.0, 0.30;
```

含义：

```text
前三个量：末端力矩相关分量
后三个量：末端线性力相关分量
```

第一次测试时建议只改一个方向，例如：

```cpp
ftip << 0.0, 0.0, 0.0, 0.20, 0.0, 0.0;
ftip << 0.0, 0.0, 0.0, 0.0, 0.20, 0.0;
ftip << 0.0, 0.0, 0.0, 0.0, 0.0, 0.20;
```

观察 Gazebo 中末端到底往哪个方向响应。

---

## 11. 如果看不到明显效果怎么办？

### 11.1 力矩太小

可以逐步增大：

```cpp
tauBias(1) = 0.04;
tauBias(1) = 0.06;
```

或者：

```cpp
ftip << 0, 0, 0, 0, 0, 0.50;
```

但不要一次加太大。

### 11.2 刚度太高

如果 Kp/Kd 太高，机械臂会很硬，力矩前馈效果不明显。

可以降低：

```cpp
const double stiffnessScale = 0.30;
const double dampingScale = 0.60;
```

### 11.3 姿态不适合观察

可以调整观察姿态：

```cpp
observeQ << 0.0, 1.20, -0.80, -0.40, 0.0, 0.0;
```

例如略微改变第 2、3、4 关节：

```cpp
observeQ << 0.0, 1.35, -1.00, -0.50, 0.0, 0.0;
```

---

## 12. 常见问题

### 12.1 `cmake ..` 提示 CMakeCache 路径不一致

错误类似：

```text
CMake Error: The current CMakeCache.txt directory ...
is different than the directory /root/work/... where CMakeCache.txt was created.
```

原因：

```text
你之前在 /root/work/... 容器路径里 cmake，
后来又在 /home/boat/... 主机路径里 cmake。
CMakeCache.txt 记录了旧的绝对路径。
```

解决：

进入容器后：

```bash
cd /root/work/unitree_z1/z1_sdk
rm -rf build
mkdir build
cd build
cmake ..
make -j$(nproc)
```

以后遵守：

```text
主机负责编辑
容器负责编译和运行
```

### 12.2 `z1_force_control` 没有生成

检查是否加入 CMakeLists：

```bash
cd /root/work/unitree_z1/z1_sdk
grep -n "z1_force_control" CMakeLists.txt
```

如果没有输出，重新编辑：

```bash
nano CMakeLists.txt
```

确保加入：

```cmake
examples/z1_force_control.cpp
```

然后重新：

```bash
cd /root/work/unitree_z1/z1_sdk/build
cmake ..
make -j$(nproc)
```

### 12.3 运行后 `sim_ctrl` 提示等待 SDK

如果终端 2 显示：

```text
connect with z1_sdk wait time out
Lose connection with z1_sdk
```

说明终端 3 的 `z1_force_control` 还没有连接。

启动终端 3：

```bash
cd /root/work/unitree_z1/z1_sdk/build
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/root/work/unitree_z1/z1_sdk/lib:/root/work/unitree_z1/z1_controller/lib
./z1_force_control
```

### 12.4 程序运行后机械臂突然动作很大

立刻停止终端 3：

```text
Ctrl + C
```

然后调小：

```cpp
tauBias(1) = 0.01;
ftip << 0, 0, 0, 0, 0, 0.10;
const double stiffnessScale = 0.60;
```

再重新编译运行。

---

## 13. 下一步：真正闭环力控怎么做？

当前 `z1_force_control.cpp` 还没有读取力反馈，因此属于：

```text
力矩前馈 / 柔顺控制入门
```

如果要做真正的末端接触力闭环，需要增加：

```text
Gazebo force_torque sensor
或 Gazebo contact sensor
或真实腕部六维力传感器
```

闭环结构通常是：

```text
F_desired 目标力
F_measured 实际力
F_error = F_desired - F_measured
根据 F_error 调整末端位姿、速度或关节力矩
```

典型导纳控制：

```text
x_dot_cmd = Kf * (F_desired - F_measured)
```

典型阻抗控制：

```text
F_cmd = Kx * (x_desired - x) + Dx * (xdot_desired - xdot)
tau = J(q)^T * F_cmd
```

建议先把本文档的 LOWCMD + tau 前馈跑通，再做传感器闭环。

---

## 14. 完整代码：z1_force_control.cpp

下面是完整代码。文件也已经单独保存为：

```text
z1_trajectory_examples/z1_force_control.cpp
```

```cpp
/*
 * Unitree Z1 力控入门示例：LOWCMD + 低刚度柔顺 + 力矩前馈。
 *
 * 这个示例基于官方 z1_sdk/examples/lowcmd_development.cpp 的控制方式。
 *
 * 运行前提：
 *   终端 1：roslaunch unitree_gazebo z1.launch
 *   终端 2：./sim_ctrl
 *   终端 3：./z1_force_control
 *
 * 本示例分 4 个阶段：
 *   1. 回到 SDK 起始姿态
 *   2. 切换到 LOWCMD 底层控制模式
 *   3. 运动到一个安全观察姿态
 *   4. 在保持姿态的同时加入小的力矩前馈和末端力前馈
 *
 * 重要说明：
 *   1. 这不是完整闭环力控，因为当前代码没有读取真实/仿真的力传感器。
 *   2. 它是力控入门第一步：学习 LOWCMD、tau_f、inverseDynamics(Ftip)。
 *   3. 请先在 Gazebo 仿真里测试，不要直接用于真实机械臂。
 */

#include "unitree_arm_sdk/control/unitreeArm.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace UNITREE_ARM;

static void clampJointPosition(Vec6& q)
{
    for (int i = 0; i < 6; ++i)
    {
        q(i) = std::max(-2.5, std::min(2.5, q(i)));
    }
}

static void printVec6(const std::string& name, const Vec6& v)
{
    std::cout << name << " = ["
              << v(0) << ", "
              << v(1) << ", "
              << v(2) << ", "
              << v(3) << ", "
              << v(4) << ", "
              << v(5) << "]" << std::endl;
}

static void moveJointLowCmd(unitreeArm& arm, const Vec6& targetQ, double moveTimeSec)
{
    Vec6 startQ = arm.lowstate->getQ();
    Vec6 goalQ = targetQ;
    clampJointPosition(goalQ);

    int steps = static_cast<int>(moveTimeSec / arm._ctrlComp->dt);
    steps = std::max(steps, 1);

    Vec6 constantQd = (goalQ - startQ) / (steps * arm._ctrlComp->dt);

    Timer timer(arm._ctrlComp->dt);

    for (int i = 0; i < steps; ++i)
    {
        double ratio = static_cast<double>(i + 1) / static_cast<double>(steps);

        arm.q = startQ * (1.0 - ratio) + goalQ * ratio;
        arm.qd = constantQd;

        arm.tau = arm._ctrlComp->armModel->inverseDynamics(
            arm.q,
            arm.qd,
            Vec6::Zero(),
            Vec6::Zero()
        );

        arm.setArmCmd(arm.q, arm.qd, arm.tau);
        arm.sendRecv();
        timer.sleep();
    }
}

static void holdPoseWithJointTorque(unitreeArm& arm, const Vec6& holdQ, const Vec6& tauBias, double holdTimeSec)
{
    int steps = static_cast<int>(holdTimeSec / arm._ctrlComp->dt);
    steps = std::max(steps, 1);

    Timer timer(arm._ctrlComp->dt);

    for (int i = 0; i < steps; ++i)
    {
        arm.q = holdQ;
        arm.qd = Vec6::Zero();

        Vec6 tauModel = arm._ctrlComp->armModel->inverseDynamics(
            arm.q,
            arm.qd,
            Vec6::Zero(),
            Vec6::Zero()
        );

        arm.tau = tauModel + tauBias;

        arm.setArmCmd(arm.q, arm.qd, arm.tau);
        arm.sendRecv();
        timer.sleep();
    }
}

static void holdPoseWithEndForce(unitreeArm& arm, const Vec6& holdQ, const Vec6& ftip, double holdTimeSec)
{
    int steps = static_cast<int>(holdTimeSec / arm._ctrlComp->dt);
    steps = std::max(steps, 1);

    Timer timer(arm._ctrlComp->dt);

    for (int i = 0; i < steps; ++i)
    {
        arm.q = holdQ;
        arm.qd = Vec6::Zero();

        arm.tau = arm._ctrlComp->armModel->inverseDynamics(
            arm.q,
            arm.qd,
            Vec6::Zero(),
            ftip
        );

        arm.setArmCmd(arm.q, arm.qd, arm.tau);
        arm.sendRecv();
        timer.sleep();
    }
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    std::cout << std::fixed << std::setprecision(4);

    bool hasGripper = true;
    unitreeArm arm(hasGripper);

    std::cout << "\n========== Z1 LOWCMD 力控入门示例 ==========\n";
    std::cout << "请确认：Gazebo 已启动，sim_ctrl 已启动。\n";
    std::cout << "本程序将进入 LOWCMD，并发送 q / qd / tau。\n";
    std::cout << "==========================================\n\n";

    arm.sendRecvThread->start();

    std::cout << "[1/6] 回到 SDK 起始姿态...\n";
    arm.backToStart();

    std::cout << "[2/6] 切换到 PASSIVE，再切换到 LOWCMD...\n";
    arm.setFsm(ArmFSMState::PASSIVE);
    arm.setFsm(ArmFSMState::LOWCMD);

    std::vector<double> KP = arm._ctrlComp->lowcmd->kp;
    std::vector<double> KW = arm._ctrlComp->lowcmd->kd;

    const double stiffnessScale = 0.45;
    const double dampingScale = 0.70;

    size_t jointCount = std::min<size_t>(6, KP.size());
    for (size_t i = 0; i < jointCount; ++i)
    {
        KP[i] *= stiffnessScale;
    }

    jointCount = std::min<size_t>(6, KW.size());
    for (size_t i = 0; i < jointCount; ++i)
    {
        KW[i] *= dampingScale;
    }

    arm._ctrlComp->lowcmd->setControlGain(KP, KW);

    arm.sendRecvThread->shutdown();

    Vec6 initQ = arm.lowstate->getQ();
    printVec6("当前关节角 initQ", initQ);

    Vec6 observeQ;
    observeQ << 0.0, 1.20, -0.80, -0.40, 0.0, 0.0;

    std::cout << "\n[3/6] LOWCMD 运动到观察姿态...\n";
    printVec6("观察姿态 observeQ", observeQ);
    moveJointLowCmd(arm, observeQ, 3.0);

    std::cout << "\n[4/6] 保持观察姿态 2 秒，只使用模型补偿...\n";
    holdPoseWithJointTorque(arm, observeQ, Vec6::Zero(), 2.0);

    std::cout << "\n[5/6] 加入小的关节力矩前馈，观察机械臂柔顺响应...\n";
    Vec6 tauBias = Vec6::Zero();
    tauBias(1) = 0.03;
    printVec6("关节力矩前馈 tauBias", tauBias);
    holdPoseWithJointTorque(arm, observeQ, tauBias, 3.0);

    std::cout << "\n[6/6] 加入很小的末端空间力前馈 Ftip，观察响应方向...\n";
    Vec6 ftip = Vec6::Zero();
    ftip << 0.0, 0.0, 0.0, 0.0, 0.0, 0.30;
    printVec6("末端空间力前馈 Ftip", ftip);
    holdPoseWithEndForce(arm, observeQ, ftip, 3.0);

    std::cout << "\n实验结束，切回 JOINTCTRL 并回到起始姿态...\n";
    arm.sendRecvThread->start();
    arm.setFsm(ArmFSMState::JOINTCTRL);
    arm.backToStart();
    arm.setFsm(ArmFSMState::PASSIVE);
    arm.sendRecvThread->shutdown();

    std::cout << "Z1 LOWCMD 力控入门示例结束。\n";
    return 0;
}
```

