#ifndef ROS2_SUBSCRIBER_CALLBACKS_HPP
#define ROS2_SUBSCRIBER_CALLBACKS_HPP

#include <std_msgs/msg/string.hpp>

/**
 * @file ros2_subscriber_callbacks.hpp
 * @brief ROS2 话题订阅回调函数声明
 *
 * 本文件集中定义所有订阅 ROS2 话题时的回调函数。
 * 在 main.cpp 或其它模块中通过 ROSManager::subscribeTopic(topic_name, CallbackName) 注册使用。
 */

/**
 * @brief 触摸唤醒话题回调
 * @param msg 话题消息
 * @note 话题名: /touch_wakeup
 */
void TouchWakeupCallback(const std_msgs::msg::String::SharedPtr msg);

/**
 * @brief AVVTN休眠话题回调
 * @param msg 话题消息
 * @note 话题名: /avvtn_sleep
 *       收到消息后将 is_sleeping 设为 true
 */
void AvvtnSleepCallback(const std_msgs::msg::String::SharedPtr msg);

/**
 * @brief 豆包TTS文本回调
 * @param msg 话题消息（纯文本）
 * @note 话题名: /doubao_tts
 *       收到文本后调用 TTS 合成并播放
 */
void DoubaoTtsCallback(const std_msgs::msg::String::SharedPtr msg);

// 在此处添加更多订阅回调的声明，例如：
// void onCommand(const std_msgs::msg::String::SharedPtr msg);
// void onConfigUpdate(const std_msgs::msg::String::SharedPtr msg);

#endif // ROS2_SUBSCRIBER_CALLBACKS_HPP
