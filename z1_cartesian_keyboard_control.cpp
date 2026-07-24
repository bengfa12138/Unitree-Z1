/*
 * Unitree Z1 笛卡尔空间键盘实时控制（使用 cartesianCtrlCmd）
 * 基于 unitreeArm.h 中提供的 cartesianCtrlCmd 函数。
 */

#include "unitree_arm_sdk/control/unitreeArm.h"
#include <algorithm>
#include <cctype>
#include <fcntl.h>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <cmath>

static termios g_oldTermios;
static int g_oldFlags = 0;

// ---------- 终端设置 ----------
static void enableKeyboardInput()
{
    tcgetattr(STDIN_FILENO, &g_oldTermios);
    termios newTermios = g_oldTermios;
    newTermios.c_lflag &= static_cast<unsigned int>(~(ICANON | ECHO));
    tcsetattr(STDIN_FILENO, TCSANOW, &newTermios);
    g_oldFlags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, g_oldFlags | O_NONBLOCK);
}

static void disableKeyboardInput()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &g_oldTermios);
    fcntl(STDIN_FILENO, F_SETFL, g_oldFlags);
}

static int readKey()
{
    unsigned char c = 0;
    int ret = read(STDIN_FILENO, &c, 1);
    if (ret == 1) return c;
    return -1;
}

static void printHelp()
{
    std::cout << "\n========== Z1 笛卡尔空间键盘控制 ==========\n"
              << "Q / A : 末端 X 方向 正 / 负\n"
              << "W / S : 末端 Y 方向 正 / 负\n"
              << "E / D : 末端 Z 方向 正 / 负\n"
              << "R / F : 末端 Roll  正 / 负\n"
              << "T / G : 末端 Pitch 正 / 负\n"
              << "Y / H : 末端 Yaw   正 / 负\n"
              << "空格  : 回到 SDK 起始姿态（并重新进入笛卡尔模式）\n"
              << "P     : 打印当前实际末端位姿\n"
              << "Esc   : 退出程序\n"
              << "============================================\n"
              << std::endl;
}

int main()
{
    UNITREE_ARM::unitreeArm arm(true);
    arm.sendRecvThread->start();

    // 先回到起始姿态，确保初始位置明确
    arm.backToStart();

    // 切换到笛卡尔空间控制模式
    arm.startTrack(UNITREE_ARM::ArmFSMState::CARTESIAN);

    // 速度常量（可根据需要调整）
    const double oriSpeed = 0.8;   // 角速度 rad/s
    const double posSpeed = 0.3;   // 线速度 m/s

    UNITREE_ARM::Timer timer(arm._ctrlComp->dt);

    enableKeyboardInput();
    printHelp();

    bool running = true;
    while (running)
    {
        // 每次循环重置 directions 为全零（停止运动）
        Vec7 directions = Vec7::Zero();

        int key = readKey();
        if (key != -1)
        {
            key = std::tolower(key);

            switch (key)
            {
                // 注意：cartesianCtrlCmd 的 directions 顺序为:
                // [roll, pitch, yaw, x, y, z, gripper]
                // 所以索引 0,1,2 为姿态，3,4,5 为位置
                case 'q': directions(3) =  1.0; break; // X+
                case 'a': directions(3) = -1.0; break; // X-
                case 'w': directions(4) =  1.0; break; // Y+
                case 's': directions(4) = -1.0; break; // Y-
                case 'e': directions(5) =  1.0; break; // Z+
                case 'd': directions(5) = -1.0; break; // Z-
                case 'r': directions(0) =  1.0; break; // Roll+
                case 'f': directions(0) = -1.0; break; // Roll-
                case 't': directions(1) =  1.0; break; // Pitch+
                case 'g': directions(1) = -1.0; break; // Pitch-
                case 'y': directions(2) =  1.0; break; // Yaw+
                case 'h': directions(2) = -1.0; break; // Yaw-

                case ' ':
                    // 回到起始姿态，完成后状态会变为 JOINTCTRL
                    arm.backToStart();
                    // 重新切换到笛卡尔模式，内部会重置参考位姿
                    arm.startTrack(UNITREE_ARM::ArmFSMState::CARTESIAN);
                    std::cout << "\n已回到起始姿态并重新进入笛卡尔模式。\n";
                    break;

                case 'p':
                    // 打印当前实际末端位姿（来自 lowstate）
                    std::cout << "\n当前实际末端位姿 (x,y,z,roll,pitch,yaw) = "
                              << arm.lowstate->endPosture.transpose() << std::endl;
                    break;

                case 27: // Esc
                    running = false;
                    break;

                default:
                    break;
            }
        }

        // 如果有按键方向，调用 cartesianCtrlCmd 发送指令
        // 即使 directions 全零，调用也没问题（会停止运动）
        arm.cartesianCtrlCmd(directions, oriSpeed, posSpeed);

        timer.sleep();
    }

    disableKeyboardInput();
    arm.setFsm(UNITREE_ARM::ArmFSMState::PASSIVE);
    arm.sendRecvThread->shutdown();

    std::cout << "\n程序已退出。\n";
    return 0;
}
