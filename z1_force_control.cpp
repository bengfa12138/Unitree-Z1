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

// 对关节角做保守限幅，避免误操作时给出过大的关节目标。
// 注意：这里不是完整的 Z1 官方关节限位，只是仿真入门保护。
static void clampJointPosition(Vec6& q)
{
    for (int i = 0; i < 6; ++i)
    {
        q(i) = std::max(-2.5, std::min(2.5, q(i)));
    }
}

// 打印 6 维向量，便于观察当前姿态或力矩。
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

// 使用 LOWCMD 从当前关节角平滑移动到目标关节角。
// 这里会用 inverseDynamics(q, qd, qdd, Ftip=0) 计算一个基础动力学补偿力矩。
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

        // 基础动力学补偿。
        // Ftip = Vec6::Zero() 表示暂时不加入末端外力。
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

// 保持某个姿态，并叠加一个小的关节力矩前馈 tauBias。
// tauBias 可以理解为“额外给某些关节推一把力”。
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

        // 总力矩 = 模型补偿力矩 + 人为加入的小力矩前馈。
        arm.tau = tauModel + tauBias;

        arm.setArmCmd(arm.q, arm.qd, arm.tau);
        arm.sendRecv();
        timer.sleep();
    }
}

// 保持某个姿态，并通过 inverseDynamics 的 Ftip 参数加入末端空间力前馈。
// Ftip 是 6 维空间力，官方接口说明为 [rpyxyz] 风格的末端空间力。
// 第一次测试时只给很小的值，并观察 Gazebo 中末端响应方向。
static void holdPoseWithEndForce(unitreeArm& arm, const Vec6& holdQ, const Vec6& ftip, double holdTimeSec)
{
    int steps = static_cast<int>(holdTimeSec / arm._ctrlComp->dt);
    steps = std::max(steps, 1);

    Timer timer(arm._ctrlComp->dt);

    for (int i = 0; i < steps; ++i)
    {
        arm.q = holdQ;
        arm.qd = Vec6::Zero();

        // 通过 Ftip 加入末端空间力前馈。
        // inverseDynamics 会把末端空间力转换成对应的关节力矩。
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

    // 先启动 SDK 默认通信线程，用于执行 backToStart 等高层动作。
    arm.sendRecvThread->start();

    std::cout << "[1/6] 回到 SDK 起始姿态...\n";
    arm.backToStart();

    std::cout << "[2/6] 切换到 PASSIVE，再切换到 LOWCMD...\n";
    arm.setFsm(ArmFSMState::PASSIVE);
    arm.setFsm(ArmFSMState::LOWCMD);

    // 读取并缩放低层控制增益。
    // 降低 Kp/Kd 可以让仿真机械臂更柔顺，也更容易观察 tau 前馈的影响。
    std::vector<double> KP = arm._ctrlComp->lowcmd->kp;
    std::vector<double> KW = arm._ctrlComp->lowcmd->kd;

    const double stiffnessScale = 0.45;  // 位置刚度缩放，越小越软。
    const double dampingScale = 0.70;    // 速度阻尼缩放，太小会抖，太大会钝。

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

    // LOWCMD 下推荐自己控制发送频率，所以关闭默认 sendRecvThread，
    // 后续每个控制周期手动调用 arm.sendRecv()。
    arm.sendRecvThread->shutdown();

    Vec6 initQ = arm.lowstate->getQ();
    printVec6("当前关节角 initQ", initQ);

    // 选择一个较保守的观察姿态，方便看到后续力矩前馈效果。
    Vec6 observeQ;
    observeQ << 0.0, 1.20, -0.80, -0.40, 0.0, 0.0;

    std::cout << "\n[3/6] LOWCMD 运动到观察姿态...\n";
    printVec6("观察姿态 observeQ", observeQ);
    moveJointLowCmd(arm, observeQ, 3.0);

    std::cout << "\n[4/6] 保持观察姿态 2 秒，只使用模型补偿...\n";
    holdPoseWithJointTorque(arm, observeQ, Vec6::Zero(), 2.0);

    std::cout << "\n[5/6] 加入小的关节力矩前馈，观察机械臂柔顺响应...\n";
    Vec6 tauBias = Vec6::Zero();

    // 给第 2 个关节一个很小的力矩前馈。
    // 如果仿真中效果很小，可以逐步改成 0.04、0.06。
    // 不建议一开始给很大数值。
    tauBias(1) = 0.03;
    printVec6("关节力矩前馈 tauBias", tauBias);
    holdPoseWithJointTorque(arm, observeQ, tauBias, 3.0);

    std::cout << "\n[6/6] 加入很小的末端空间力前馈 Ftip，观察响应方向...\n";
    Vec6 ftip = Vec6::Zero();

    // Ftip 的前三个量可理解为末端力矩相关分量，后三个量为末端线性力相关分量。
    // 这里先给一个很小的 z 方向末端力前馈。
    // 坐标方向需要你在 Gazebo 中观察确认。
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

