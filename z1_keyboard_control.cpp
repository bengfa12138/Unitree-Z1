/*
 * Unitree Z1 键盘实时控制示例。
 *
 * 作用：
 *   运行本程序后，可以在终端里按 Q/A、W/S、E/D、R/F、T/G、Y/H
 *   实时调整 Z1 六个关节的目标角度。
 *
 * 运行前提：
 *   终端 1：roslaunch unitree_gazebo z1.launch
 *   终端 2：./sim_ctrl
 *   终端 3：./z1_keyboard_control
 *
 * 注意：
 *   1. 按键要在运行本程序的终端里输入，不是在 Gazebo 窗口里输入。
 *   2. 本程序建议先用于仿真。
 *   3. 如果用于真实机械臂，请降低 jointSpeed，并确保周围安全。
 */

#include "unitree_arm_sdk/control/unitreeArm.h"

#include <algorithm>
#include <cctype>
#include <fcntl.h>
#include <iostream>
#include <termios.h>
#include <unistd.h>

// 保存终端原始配置，程序退出时恢复。
static termios g_oldTermios;
static int g_oldFlags = 0;

// 将终端切换为“非阻塞、无需回车、无回显”模式。
// 这样程序可以实时读取单个按键。
static void enableKeyboardInput()
{
    tcgetattr(STDIN_FILENO, &g_oldTermios);

    termios newTermios = g_oldTermios;
    newTermios.c_lflag &= static_cast<unsigned int>(~(ICANON | ECHO));
    tcsetattr(STDIN_FILENO, TCSANOW, &newTermios);

    g_oldFlags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, g_oldFlags | O_NONBLOCK);
}

// 恢复终端配置，避免程序退出后终端显示异常。
static void disableKeyboardInput()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &g_oldTermios);
    fcntl(STDIN_FILENO, F_SETFL, g_oldFlags);
}

// 尝试读取一个按键。
// 如果当前没有按键，返回 -1。
static int readKey()
{
    unsigned char c = 0;
    int ret = read(STDIN_FILENO, &c, 1);
    if (ret == 1)
    {
        return c;
    }
    return -1;
}

// 对关节角做一个简单限幅，避免误操作时角度变得太夸张。
// 这里不是严格的 Z1 官方关节限位，只是仿真测试用的保守保护。
static void clampJointPosition(Vec6& q)
{
    for (int i = 0; i < 6; ++i)
    {
        q(i) = std::max(-2.5, std::min(2.5, q(i)));
    }
}

static void printHelp()
{
    std::cout << "\n========== Z1 键盘实时控制 ==========\n"
              << "按键需要在当前终端中输入，不是在 Gazebo 窗口中输入。\n\n"
              << "Q / A : 第 1 关节 正 / 反方向\n"
              << "W / S : 第 2 关节 正 / 反方向\n"
              << "E / D : 第 3 关节 正 / 反方向\n"
              << "R / F : 第 4 关节 正 / 反方向\n"
              << "T / G : 第 5 关节 正 / 反方向\n"
              << "Y / H : 第 6 关节 正 / 反方向\n"
              << "空格  : 回到 SDK 起始姿态\n"
              << "P     : 打印当前目标关节角\n"
              << "Esc   : 退出程序\n"
              << "====================================\n"
              << std::endl;
}

int main()
{
    UNITREE_ARM::unitreeArm arm(true);
    arm.sendRecvThread->start();

    // 先回到 SDK 预定义起始姿态，避免从未知姿态开始。
    arm.backToStart();

    // 切换到关节空间控制模式。
    arm.startTrack(UNITREE_ARM::ArmFSMState::JOINTCTRL);

    // 当前命令关节角，以机械臂当前角度为初始值。
    Vec6 cmdQ = arm.lowstate->getQ();

    // 当前命令关节速度。
    Vec6 cmdQd = Vec6::Zero();

    // 每个关节的运动速度，单位 rad/s。
    // 想让按键更灵敏可以调大，想更安全平滑可以调小。
    const double jointSpeed = 0.35;

    UNITREE_ARM::Timer timer(arm._ctrlComp->dt);

    enableKeyboardInput();
    printHelp();

    bool running = true;
    while (running)
    {
        cmdQd.setZero();

        int key = readKey();
        if (key != -1)
        {
            key = std::tolower(key);

            switch (key)
            {
            case 'q':
                cmdQd(0) = jointSpeed;
                break;
            case 'a':
                cmdQd(0) = -jointSpeed;
                break;

            case 'w':
                cmdQd(1) = jointSpeed;
                break;
            case 's':
                cmdQd(1) = -jointSpeed;
                break;

            case 'e':
                cmdQd(2) = jointSpeed;
                break;
            case 'd':
                cmdQd(2) = -jointSpeed;
                break;

            case 'r':
                cmdQd(3) = jointSpeed;
                break;
            case 'f':
                cmdQd(3) = -jointSpeed;
                break;

            case 't':
                cmdQd(4) = jointSpeed;
                break;
            case 'g':
                cmdQd(4) = -jointSpeed;
                break;

            case 'y':
                cmdQd(5) = jointSpeed;
                break;
            case 'h':
                cmdQd(5) = -jointSpeed;
                break;

            case ' ':
                // 空格：回到起始姿态，并重新读取当前关节角作为控制起点。
                arm.backToStart();
                cmdQ = arm.lowstate->getQ();
                cmdQd.setZero();
                std::cout << "\n已回到起始姿态。\n";
                break;

            case 'p':
                std::cout << "\n当前目标关节角 q = "
                          << cmdQ.transpose() << std::endl;
                break;

            case 27:
                // Esc 键。
                running = false;
                break;

            default:
                break;
            }
        }

        // 根据速度积分得到新的目标关节角。
        cmdQ += cmdQd * arm._ctrlComp->dt;
        clampJointPosition(cmdQ);

        arm.q = cmdQ;
        arm.qd = cmdQd;
        arm.setArmCmd(arm.q, arm.qd);

        timer.sleep();
    }

    disableKeyboardInput();

    arm.setFsm(UNITREE_ARM::ArmFSMState::PASSIVE);
    arm.sendRecvThread->shutdown();

    std::cout << "\nZ1 键盘控制程序已退出。\n";
    return 0;
}

