/*
 * Unitree Z1 自定义轨迹示例，中文注释版。
 *
 * 源码参考：
 *   https://github.com/unitreerobotics/z1_sdk/blob/master/examples/highcmd_development.cpp
 *
 * 使用思路：
 *   1. 将本文件复制到 Docker 容器内的 /root/work/unitree_z1/z1_sdk/examples/ 目录。
 *   2. 可以直接替换官方 highcmd_development.cpp，也可以在 CMakeLists.txt 中新增可执行文件。
 *   3. 重新编译 z1_sdk，并在 Gazebo + sim_ctrl 已经启动后运行。
 *
 * 安全提示：
 *   本示例建议先在仿真中测试。若用于真实 Z1 机械臂，请降低速度，
 *   保守设置关节目标，并确保机械臂周围有足够安全空间。
 */

#include "unitree_arm_sdk/control/unitreeArm.h"

int main()
{
    // 创建 Z1 机械臂对象。
    // true 参数沿用官方示例，用于启用 SDK 通信相关初始化。
    UNITREE_ARM::unitreeArm arm(true);

    // 启动 SDK 的发送/接收线程。
    // 没有这个线程，控制命令和机械臂状态不会持续交换。
    arm.sendRecvThread->start();

    // 让机械臂回到 SDK 预定义的起始姿态。
    // 在仿真中这样做可以避免从未知关节姿态突然开始运动。
    arm.backToStart();

    // 切换到关节空间控制模式。
    // 在该模式下，可以直接给 6 个关节发送角度和角速度命令。
    arm.startTrack(UNITREE_ARM::ArmFSMState::JOINTCTRL);

    // duration 表示控制周期数量，不是秒数。
    // 实际运动时间约为：
    //     duration * arm._ctrlComp->dt
    // 如果 dt 为 0.002 秒，则 1000 个周期约等于 2 秒。
    double duration = 1000.0;

    // Vec6 是 Z1 SDK 使用的 6 维向量。
    // 在关节空间控制中，6 个数分别对应 joint1 到 joint6。
    Vec6 targetPos, lastPos;

    // 读取当前关节角，作为本段轨迹的起点。
    // 这样可以避免从假设姿态直接跳变到目标姿态。
    lastPos = arm.lowstate->getQ();

    // 设置目标关节角。
    // 这里是自定义机械臂目标姿态的主要位置。
    //
    // 格式：
    //   targetPos << joint1, joint2, joint3, joint4, joint5, joint6;
    //
    // 单位：
    //   弧度，不是角度。
    //
    // 示例：
    //   1.57 rad 约等于 90 度。
    targetPos << 0.0, 1.5, -1.0, -0.54, 0.0, 0.0;

    // Timer 用于保持循环周期与 SDK 控制周期一致。
    UNITREE_ARM::Timer timer(arm._ctrlComp->dt);

    // 从 lastPos 到 targetPos 做线性插值。
    // 每次循环发送一次关节位置命令和关节速度命令。
    for (int i = 0; i < duration; i++)
    {
        // ratio 从 0.0 逐渐变化到接近 1.0。
        // 它表示当前已经走完了整段轨迹的多少比例。
        double ratio = i / duration;

        // 关节位置命令：
        //   ratio = 0 时，q = lastPos
        //   ratio = 1 时，q = targetPos
        arm.q = lastPos * (1.0 - ratio) + targetPos * ratio;

        // 关节速度命令：
        //   在 duration 个控制周期内，从 lastPos 匀速运动到 targetPos。
        //   dt 是每个控制周期的实际时长。
        arm.qd = (targetPos - lastPos) / (duration * arm._ctrlComp->dt);

        // 将关节位置和速度命令发送给机械臂。
        // 在仿真中，sim_ctrl 会把这个命令转发给 Gazebo 控制器。
        arm.setArmCmd(arm.q, arm.qd);

        // 等待下一个控制周期。
        timer.sleep();
    }

    // 到达目标姿态后，回到预定义起始姿态。
    // 如果希望机械臂停留在 targetPos，可以注释掉这一行。
    arm.backToStart();

    // 程序退出前切换到被动状态。
    arm.setFsm(UNITREE_ARM::ArmFSMState::PASSIVE);

    // 干净地关闭 SDK 通信线程。
    arm.sendRecvThread->shutdown();

    return 0;
}
