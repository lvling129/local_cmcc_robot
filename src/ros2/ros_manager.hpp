#ifndef ROS_MANAGER_HPP
#define ROS_MANAGER_HPP

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

class ROSManager {
public:
    // 获取单例实例
    static ROSManager& getInstance();
    
    // 初始化ROS管理器（在main函数中调用，线程安全，仅执行一次）
    void init(int argc, char const *argv[]);
    
    // 获取节点
    std::shared_ptr<rclcpp::Node> getNode();
    
    // 发布日志
    void publishLog(const std::string& log_msg);
    
    // 发布聊天历史
    void publishChatHistory(const std::string& chat_msg);
    
    // 发布状态
    void publishStatus(const std::string& status_msg);

    // 发布聊天历史(非流式文本)
    void publishChatHistoryNoStream(const std::string& chat_msg);

    // 发布带角度的唤醒消息给PC2做转向动作
    void publishWakeupDetail(const std::string& chat_msg);

    // 发布语音唤醒事件（topic: /voice_wakeup）
    void publishVoiceWakeup(const std::string& msg);
    
    // 订阅话题
    void subscribeTopic(const std::string& topic_name, 
                       std::function<void(const std_msgs::msg::String::SharedPtr)> callback);
    
    // 关闭ROS
    void shutdown();
    
private:
    ROSManager() = default;
    ~ROSManager() = default;
    
    // 禁用拷贝和赋值
    ROSManager(const ROSManager&) = delete;
    ROSManager& operator=(const ROSManager&) = delete;

    // 内部发布方法（调用者需自行持有 pub_mutex_ 的读锁或确保已初始化）
    void publishMessage(const rclcpp::Publisher<std_msgs::msg::String>::SharedPtr& publisher,
                        const std::string& msg);

    std::shared_ptr<rclcpp::Node> node_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr log_publisher_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr chat_history_publisher_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr status_publisher_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr chat_history_nostream_publisher_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr wakeup_detail_publisher_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr voice_wakeup_publisher_;

    // 保存订阅者
    std::unordered_map<std::string, rclcpp::Subscription<std_msgs::msg::String>::SharedPtr> subscribers_;

    // --- 线程安全 ---
    std::atomic<bool> initialized_{false};       // 原子标志，用于快速判断是否已初始化
    std::once_flag init_flag_;                   // 保证 init() 只执行一次
    mutable std::shared_mutex pub_mutex_;        // 读写锁：保护 publisher 的并发读 / shutdown 的写
    mutable std::mutex sub_mutex_;               // 互斥锁：保护 subscribers_ map
    std::thread ros_spin_thread_;
};

#endif // ROS_MANAGER_HPP
