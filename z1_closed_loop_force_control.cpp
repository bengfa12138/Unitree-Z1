/*
 * Unitree Z1 闭环力控示例：Gazebo FT sensor + ROS 订阅 + LOWCMD 力矩控制。
 *
 * 运行前提：
 *   1. 已经在 Z1 的 gazebo.xacro 中加入 libgazebo_ros_ft_sensor.so。
 *   2. Gazebo 中存在 /z1/ft_sensor 话题：
 *        rostopic echo /z1/ft_sensor
 *   3. 终端 1：roslaunch unitree_gazebo z1.launch
 *   4. 终端 2：./sim_ctrl
 *   5. 终端 3：./z1_closed_loop_force_control
 *
 * 控制思路：
 *   desiredWrench = 键盘设定的目标末端力/力矩
 *   measuredWrench = Gazebo FT sensor 测得的实际力/力矩，减去 tare 零偏
 *   error = desiredWrench - measuredWrench
 *   commandWrench = desiredWrench + Kf * error
 *   tau = inverseDynamics(q, qd, qdd=0, commandWrench)
 *
 * 重要说明：
 *   1. 这是仿真闭环力控示例，不要直接用于真实机械臂。
 *   2. FT sensor 的正负方向可能和控制命令方向相反，运行时可以按 I 反向传感器符号。
 *   3. 如果没有接触物体，力传感器读数通常不会接近目标力；请让末端接触方块/桌面后再观察闭环效果。
 */

#include "unitree_arm_sdk/control/unitreeArm.h"

#include <geometry_msgs/WrenchStamped.h>
#include <ros/ros.h>

#include <algorithm>
#include <atexit.h>
#include <cctype>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <vector>

using namespace UNITREE_ARM;

static termios g_oldTermios;
static int g_oldFlags = 0;
static bool g_keyboardEnabled = false;

static Vec6 g_rawWrench = Vec6::Zero();
static ros::Time g_lastWrenchTime;
static bool g_hasWrench = false;

static void ftSensorCallback(const geometry_msgs::WrenchStamped::ConstPtr& msg)
{
    g_rawWrench(0) = msg->wrench.torque.x;
    g_rawWrench(1) = msg->wrench.torque.y;
    g_rawWrench(2) = msg->wrench.torque.z;
    g_rawWrench(3) = msg->wrench.force.x;
    g_rawWrench(4) = msg->wrench.force.y;
    g_rawWrench(5) = msg->wrench.force.z;
    g_lastWrenchTime = ros::Time::now();
    g_hasWrench = true;
}

static void enableKeyboardInput()
{
    if (g_keyboardEnabled)
    {
        return;
    }

    tcgetattr(STDIN_FILENO, &g_oldTermios);

    termios newTermios = g_oldTermios;
    newTermios.c_lflag &= static_cast<unsigned int>(~(ICANON | ECHO));
    tcsetattr(STDIN_FILENO, TCSANOW, &newTermios);

    g_oldFlags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, g_oldFlags | O_NONBLOCK);

    g_keyboardEnabled = true;
}

static void disableKeyboardInput()
{
    if (!g_keyboardEnabled)
    {
        return;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &g_oldTermios);
    fcntl(STDIN_FILENO, F_SETFL, g_oldFlags);
    g_keyboardEnabled = false;
}

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

static double clampValue(double value, double limit)
{
    return std::max(-limit, std::min(limit, value));
}

static void clampWrench(Vec6& wrench, double momentLimit, double forceLimit)
{
    for (int i = 0; i < 3; ++i)
    {
        wrench(i) = clampValue(wrench(i), momentLimit);
    }
    for (int i = 3; i < 6; ++i)
    {
        wrench(i) = clampValue(wrench(i), forceLimit);
    }
}

static void clampJointPosition(Vec6& q)
{
    for (int i = 0; i < 6; ++i)
    {
        q(i) = clampValue(q(i), 2.5);
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

static void printHelp()
{
    std::cout << "\n========== Z1 Gazebo FT Sensor 闭环力控 ==========\n"
              << "按键需要在运行本程序的终端输入，不是在 Gazebo 窗口输入。\n\n"
              << "目标末端线性力 desiredWrench 后三项：\n"
              << "  W / S : Fx + / -\n"
              << "  A / D : Fy + / -\n"
              << "  R / F : Fz + / -\n\n"
              << "目标末端转动力矩 desiredWrench 前三项：\n"
              << "  Q / E : Mz + / -\n\n"
              << "闭环和传感器：\n"
              << "  B     : 以当前 FT sensor 读数为零点 tare\n"
              << "  I     : 反转 measuredWrench 符号，方向不对时使用\n"
              << "  C     : 切换柔顺档位 very_soft / soft / normal\n"
              << "  0     : 清零目标力和命令力，但不清零 tare\n\n"
              << "其他：\n"
              << "  P     : 打印 raw/tare/measured/desired/command\n"
              << "  H     : 显示帮助\n"
              << "  Esc   : 退出程序\n"
              << "=================================================\n"
              << std::endl;
}

static const char* complianceName(int level)
{
    if (level == 0)
    {
        return "very_soft";
    }
    if (level == 2)
    {
        return "normal";
    }
    return "soft";
}

static void applyComplianceGain(
    unitreeArm& arm,
    const std::vector<double>& baseKP,
    const std::vector<double>& baseKW,
    int level)
{
    std::vector<double> KP = baseKP;
    std::vector<double> KW = baseKW;

    double stiffnessScale = 0.35;
    double dampingScale = 0.65;

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

    std::cout << "\n当前柔顺档位：" << complianceName(level)
              << "，Kp 缩放 = " << stiffnessScale
              << "，Kd 缩放 = " << dampingScale << std::endl;
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

    for (int i = 0; i < steps && ros::ok(); ++i)
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
        ros::spinOnce();
        timer.sleep();
    }
}

static void printStatus(
    const Vec6& rawWrench,
    const Vec6& tareWrench,
    const Vec6& measuredWrench,
    const Vec6& desiredWrench,
    const Vec6& commandWrench,
    int complianceLevel,
    double sensorSign)
{
    std::cout << "\n当前状态：\n";
    std::cout << "  柔顺档位 = " << complianceName(complianceLevel) << "\n";
    std::cout << "  sensorSign = " << sensorSign << "\n";
    std::cout << "  FT sensor 状态 = " << (g_hasWrench ? "已收到数据" : "还没有收到数据") << "\n";
    printVec6("  raw      [Mx, My, Mz, Fx, Fy, Fz]", rawWrench);
    printVec6("  tare     [Mx, My, Mz, Fx, Fy, Fz]", tareWrench);
    printVec6("  measured [Mx, My, Mz, Fx, Fy, Fz]", measuredWrench);
    printVec6("  desired  [Mx, My, Mz, Fx, Fy, Fz]", desiredWrench);
    printVec6("  command  [Mx, My, Mz, Fx, Fy, Fz]", commandWrench);
}

int main(int argc, char** argv)
{
    ros::init(argc, argv, "z1_closed_loop_force_control");
    ros::NodeHandle nh;
    ros::NodeHandle privateNh("~");

    std::cout << std::fixed << std::setprecision(4);
    std::atexit(disableKeyboardInput);

    std::string ftTopic = "/z1/ft_sensor";
    privateNh.param<std::string>("ft_topic", ftTopic, ftTopic);

    ros::Subscriber ftSub = nh.subscribe(ftTopic, 10, ftSensorCallback);

    bool hasGripper = true;
    unitreeArm arm(hasGripper);

    std::cout << "\n========== Z1 Gazebo FT Sensor 闭环力控启动 ==========\n";
    std::cout << "订阅 FT sensor 话题：" << ftTopic << "\n";
    std::cout << "请确认 Gazebo、/z1/ft_sensor 和 sim_ctrl 已经启动。\n";
    std::cout << "=====================================================\n";

    arm.sendRecvThread->start();

    std::cout << "\n[1/5] 回到 SDK 起始姿态...\n";
    arm.backToStart();

    std::cout << "[2/5] 切换到 LOWCMD...\n";
    arm.setFsm(ArmFSMState::PASSIVE);
    arm.setFsm(ArmFSMState::LOWCMD);

    std::vector<double> baseKP = arm._ctrlComp->lowcmd->kp;
    std::vector<double> baseKW = arm._ctrlComp->lowcmd->kd;

    int complianceLevel = 1;
    applyComplianceGain(arm, baseKP, baseKW, complianceLevel);

    arm.sendRecvThread->shutdown();

    Vec6 observeQ;
    observeQ << 0.0, 1.20, -0.80, -0.40, 0.0, 0.0;

    std::cout << "\n[3/5] 移动到力控观察姿态...\n";
    printVec6("观察姿态 observeQ", observeQ);
    moveJointLowCmd(arm, observeQ, 3.0);

    std::cout << "\n[4/5] 等待 FT sensor 数据...\n";
    ros::Rate waitRate(100);
    ros::Time waitStart = ros::Time::now();
    while (ros::ok() && !g_hasWrench && (ros::Time::now() - waitStart).toSec() < 5.0)
    {
        ros::spinOnce();
        waitRate.sleep();
    }

    if (!g_hasWrench)
    {
        std::cout << "警告：5 秒内没有收到 FT sensor 数据。\n"
                  << "程序仍会运行，但 measuredWrench 会保持为 0，请检查 /z1/ft_sensor。\n";
    }

    Vec6 holdQ = observeQ;
    Vec6 tareWrench = g_hasWrench ? g_rawWrench : Vec6::Zero();
    Vec6 desiredWrench = Vec6::Zero();
    Vec6 commandWrench = Vec6::Zero();

    const double forceStep = 0.50;
    const double momentStep = 0.08;
    const double desiredForceLimit = 8.00;
    const double desiredMomentLimit = 1.00;
    const double commandForceLimit = 12.00;
    const double commandMomentLimit = 1.50;

    const double forceFeedbackGain = 0.70;
    const double momentFeedbackGain = 0.45;

    double sensorSign = 1.0;
    bool running = true;
    bool printedNoSensorWarning = false;

    enableKeyboardInput();
    printHelp();

    std::cout << "\n[5/5] 进入闭环力控循环。建议先按 B 标定零点，再让末端接触方块。\n";

    Timer timer(arm._ctrlComp->dt);

    while (running && ros::ok())
    {
        ros::spinOnce();

        Vec6 rawWrench = g_hasWrench ? g_rawWrench : Vec6::Zero();
        Vec6 measuredWrench = sensorSign * (rawWrench - tareWrench);

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

        clampWrench(commandWrench, commandMomentLimit, commandForceLimit);

        int key = readKey();
        if (key != -1)
        {
            key = std::tolower(key);

            switch (key)
            {
            case 'w':
                desiredWrench(3) += forceStep;
                break;
            case 's':
                desiredWrench(3) -= forceStep;
                break;
            case 'a':
                desiredWrench(4) += forceStep;
                break;
            case 'd':
                desiredWrench(4) -= forceStep;
                break;
            case 'r':
                desiredWrench(5) += forceStep;
                break;
            case 'f':
                desiredWrench(5) -= forceStep;
                break;
            case 'q':
                desiredWrench(2) += momentStep;
                break;
            case 'e':
                desiredWrench(2) -= momentStep;
                break;

            case 'b':
                tareWrench = g_hasWrench ? g_rawWrench : Vec6::Zero();
                std::cout << "\n已用当前 FT sensor 读数作为零点 tare。\n";
                break;

            case 'i':
                sensorSign *= -1.0;
                std::cout << "\n已反转 measuredWrench 符号，sensorSign = " << sensorSign << "\n";
                break;

            case 'c':
                complianceLevel = (complianceLevel + 1) % 3;
                applyComplianceGain(arm, baseKP, baseKW, complianceLevel);
                break;

            case '0':
                desiredWrench.setZero();
                commandWrench.setZero();
                std::cout << "\n已清零 desiredWrench 和 commandWrench。\n";
                break;

            case 'p':
                printStatus(
                    rawWrench,
                    tareWrench,
                    measuredWrench,
                    desiredWrench,
                    commandWrench,
                    complianceLevel,
                    sensorSign
                );
                break;

            case 'h':
                printHelp();
                break;

            case 27:
                running = false;
                break;

            default:
                break;
            }

            clampWrench(desiredWrench, desiredMomentLimit, desiredForceLimit);

            if (key != 'p' && key != 'h' && key != 27)
            {
                printStatus(
                    rawWrench,
                    tareWrench,
                    measuredWrench,
                    desiredWrench,
                    commandWrench,
                    complianceLevel,
                    sensorSign
                );
            }
        }

        if (!g_hasWrench && !printedNoSensorWarning)
        {
            std::cout << "\n警告：还没有收到 FT sensor 数据，闭环暂时退化成开环命令。\n";
            printedNoSensorWarning = true;
        }

        arm.q = holdQ;
        arm.qd = Vec6::Zero();
        arm.tau = arm._ctrlComp->armModel->inverseDynamics(
            arm.q,
            arm.qd,
            Vec6::Zero(),
            commandWrench
        );

        arm.setArmCmd(arm.q, arm.qd, arm.tau);
        arm.sendRecv();
        timer.sleep();
    }

    disableKeyboardInput();

    std::cout << "\n退出前清零命令力，并切回 PASSIVE...\n";
    for (int i = 0; i < 200; ++i)
    {
        arm.q = holdQ;
        arm.qd = Vec6::Zero();
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

    arm.sendRecvThread->start();
    arm.setFsm(ArmFSMState::JOINTCTRL);
    arm.backToStart();
    arm.setFsm(ArmFSMState::PASSIVE);
    arm.sendRecvThread->shutdown();

    std::cout << "Z1 Gazebo FT Sensor 闭环力控程序已退出。\n";
    return 0;
}
