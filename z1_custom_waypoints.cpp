/*
 * Unitree Z1 自定义多路点轨迹示例。
 *
 * 本文件基于官方 highcmd_development.cpp 的控制方式：
 *   关节空间控制 + 线性插值 + setArmCmd(q, qd)
 *
 * 修改 p1、p2、p3 等路点的数值即可自定义机械臂动作。
 * 所有关节角单位都是弧度。
 */

#include "unitree_arm_sdk/control/unitreeArm.h"

#include <algorithm>
#include <vector>

// 使用线性插值，从当前关节角运动到 targetPos。
static void moveJointLinear(UNITREE_ARM::unitreeArm& arm, const Vec6& targetPos, double moveTimeSec)
{
    // 读取当前关节角，作为本段轨迹的起点。
    Vec6 startPos = arm.lowstate->getQ();

    // 将运动时间从秒转换为控制周期数量。
    // SDK 的控制周期为 arm._ctrlComp->dt。
    int steps = static_cast<int>(moveTimeSec / arm._ctrlComp->dt);
    steps = std::max(steps, 1);

    // 本段轨迹的恒定关节速度。
    Vec6 qd = (targetPos - startPos) / (steps * arm._ctrlComp->dt);

    UNITREE_ARM::Timer timer(arm._ctrlComp->dt);

    for (int i = 0; i < steps; ++i)
    {
        // ratio 从 0 逐渐变化到 1。
        double ratio = static_cast<double>(i + 1) / static_cast<double>(steps);

        // 线性插值：
        // q = start * (1-ratio) + target * ratio
        arm.q = startPos * (1.0 - ratio) + targetPos * ratio;
        arm.qd = qd;

        // 向 Z1 发送控制命令。
        // 在仿真中，sim_ctrl 会把该命令转发给 Gazebo。
        arm.setArmCmd(arm.q, arm.qd);
        timer.sleep();
    }
}

int main()
{
    UNITREE_ARM::unitreeArm arm(true);
    arm.sendRecvThread->start();

    // 先回到 SDK 预定义的起始姿态。
    arm.backToStart();

    // 使用关节空间控制模式。
    arm.startTrack(UNITREE_ARM::ArmFSMState::JOINTCTRL);

    // ---------------- 在这里自定义轨迹 ----------------
    //
    // 每个路点格式如下：
    //   p << joint1, joint2, joint3, joint4, joint5, joint6;
    //
    // 单位：
    //   弧度。
    //
    // 角度换算参考：
    //   30 度 = 0.524 rad
    //   45 度 = 0.785 rad
    //   90 度 = 1.571 rad
    //
    // 第一次测试时，建议在仿真中使用小幅度、慢速度的动作。

    Vec6 p1, p2, p3, p4;

    // 姿态 1：抬起机械臂。
    p1 << 0.0, 1.2, -0.8, -0.4, 0.0, 0.0;

    // 姿态 2：旋转底座关节，并调整肘部姿态。
    p2 << 0.45, 1.35, -1.05, -0.55, 0.15, 0.0;

    // 姿态 3：移动到另一侧。
    p3 << -0.45, 1.25, -0.95, -0.45, -0.15, 0.0;

    // 姿态 4：回到接近中间的位置。
    p4 << 0.0, 1.0, -0.75, -0.35, 0.0, 0.0;

    std::vector<Vec6> waypoints = {p1, p2, p3, p4};

    // 每段轨迹的运动时间，单位为秒。
    // 数值越大，运动越慢，也越平滑。
    std::vector<double> segmentTimes = {2.0, 2.5, 2.5, 2.0};

    // -----------------------------------------------------------

    for (size_t i = 0; i < waypoints.size(); ++i)
    {
        moveJointLinear(arm, waypoints[i], segmentTimes[i]);
    }

    // 轨迹结束后回到起始姿态。
    // 如果希望机械臂停在最后一个路点，可以注释掉这一行。
    arm.backToStart();

    arm.setFsm(UNITREE_ARM::ArmFSMState::PASSIVE);
    arm.sendRecvThread->shutdown();

    return 0;
}
