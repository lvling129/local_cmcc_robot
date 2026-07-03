#include <condition_variable>
#include <csignal>
#include <iostream>
#include <mutex>
#include "utils/Logger.hpp"
#include "ros2/ros_manager.hpp"
#include "ros2/ros2_subscriber_callbacks.hpp"
#include "avvtn_capture/avvtn_capture.h"

static std::mutex mutex_;
static std::condition_variable cv_;

void signalHandle(int num)
{
    std::cout << "Caught signal " << num << std::endl;
    cv_.notify_one();
}

void TestLogger() {
    // 记录不同级别的日志
    LOG_TRACE("这是一个TRACE日志: {}", 42);
    LOG_DEBUG("调试信息: {}", "value");
    LOG_INFO("程序启动");
    LOG_WARN("警告信息");
    LOG_ERROR("错误发生");
    LOG_FATAL("致命错误!");
}

void InitLogger() {
    // 初始化日志系统
    auto& logger = Logger::Logger::GetInstance();

    // 设置日志级别
    logger.SetLevel(Logger::LogLevel::LOG_INFO);

    // 添加控制台输出（带颜色）
    logger.AddSink(std::unique_ptr<Logger::ConsoleSink>(new Logger::ConsoleSink(true)));

    // 添加文件输出（10MB大小限制，保留5个备份文件）
    logger.AddSink(std::unique_ptr<Logger::FileSink>(new Logger::FileSink("app.log", 10 * 1024 * 1024, 5, false)));

    // 设置异步模式（提高性能）
    logger.SetAsyncMode(true);

    // 时间格式
    //logger.SetTimeFormat("%Y-%m-%d %H:%M:%S.%f");

    // 是否显示毫秒
    //logger.SetShowMilliseconds(true);

    // 是否显示微秒
    //logger.SetShowMicroseconds(false);

    // 是否显示文件名和行号
    //logger.SetShowSourceLocation(true);

    // 是否显示线程ID
    //logger.SetShowThreadId(true);

    // 日志对齐格式
    //logger.SetPaddingWidth(20); // 消息对齐宽度

    LOG_INFO("日志系统初始化成功");

    logger.Flush();
}

int main(int argc, char const *argv[])
{
    signal(SIGINT, signalHandle);

    // 1. 初始化日志系统
    InitLogger();
    //TestLogger();

    // 2. 初始化ROS管理器
    ROSManager::getInstance().init(argc, argv);

    // 3. 在这里订阅ROS2话题
    ROSManager::getInstance().subscribeTopic("wake_up_turn_result", WakeUpResultCallback);
    ROSManager::getInstance().subscribeTopic("touch_wake_topic", TouchWakeupCallback);
    ROSManager::getInstance().subscribeTopic("/avvtn_sleep", AvvtnSleepCallback);

    // 4. 初始化AvvtnCapture
    AvvtnCapture capture;

    int ret = capture.Init("/home/nvidia/local_cmcc_robot/avvtn.cfg", "/home/nvidia/local_cmcc_robot/resource/aiui/aiui.cfg");
    if (ret != 0)
    {
        LOG_FATAL("Avvtn capture init Error");
        capture.Destory();
        ROSManager::getInstance().shutdown();
        return -1;
    }

    // 5.1 订阅豆包TTS话题（需在 AvvtnCapture 初始化后）
    ROSManager::getInstance().subscribeTopic("/doubao_tts", DoubaoTtsCallback);

    // 6. 等待信号
    std::unique_lock<std::mutex> lk(mutex_);
    cv_.wait(lk);

    // 6. 清理
    capture.Destory();
    ROSManager::getInstance().shutdown();
    LOG_INFO("结束程序");
    return 0;
}
