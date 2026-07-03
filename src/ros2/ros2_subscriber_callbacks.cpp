#include "ros2_subscriber_callbacks.hpp"
#include "utils/Logger.hpp"
#include "utils/json.hpp"
#include <iostream>
#include "avvtn_capture/avvtn_capture.h"
#include "ros2/ros_manager.hpp"

void WakeUpResultCallback(const std_msgs::msg::String::SharedPtr msg)
{
    LOG_INFO("收到唤醒结果: {%s}", msg->data.c_str());
    if (msg->data == "success") {
        LOG_INFO("唤醒结果为 success");
        int ret = 0;

        if (AvvtnCapture::getInstance()) {
            // 设置波束为正前方beam为0
            ret = AvvtnCapture::getInstance()->test_set_beam("{\"params\":{\"beam\":\"0\"}}");
        } else {
            LOG_ERROR("g_avvtn_capture_instance is nullptr, cannot set beam");
        }
        if (ret == 0)
        {
            LOG_INFO("波束设置为正前方,beam_id=0");   
        } else {
            LOG_ERROR("Failed to set beam");
        }
    } else {
        LOG_WARN("唤醒结果不是 success, 不设置波束");
    }
}

void TouchWakeupCallback(const std_msgs::msg::String::SharedPtr msg)
{
    LOG_INFO("收到触摸唤醒话题: {%s}", msg->data.c_str());
    if (AvvtnCapture::getInstance()) {
        AvvtnCapture::getInstance()->handleTouchWake();
    } else {
        LOG_ERROR("AvvtnCapture 实例为空，无法处理触摸唤醒");
    }
}

void AvvtnSleepCallback(const std_msgs::msg::String::SharedPtr msg)
{
    LOG_INFO("收到AVVTN休眠话题: {%s}", msg->data.c_str());
    if (AvvtnCapture::getInstance()) {
        AvvtnCapture::getInstance()->setSleeping(true);
        LOG_INFO("is_sleeping 已设为 true");
    } else {
        LOG_ERROR("AvvtnCapture 实例为空，无法设置休眠状态");
    }
}

void DoubaoTtsCallback(const std_msgs::msg::String::SharedPtr msg)
{
    LOG_INFO("收到豆包TTS文本: {%s}", msg->data.c_str());
    if (msg->data.empty()) {
        LOG_WARN("豆包TTS文本为空，跳过");
        return;
    }

    // 发布到 /chat_history 话题
    nlohmann::json chat_tts = {
        {"speaker", "ROBOT"},
        {"content", msg->data}
    };
    ROSManager::getInstance().publishChatConversation(chat_tts.dump());

    if (AvvtnCapture::getInstance()) {
        AvvtnCapture::getInstance()->Speak(msg->data);
    } else {
        LOG_ERROR("AvvtnCapture 实例为空，无法播放TTS");
    }
}
