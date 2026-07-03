#include "ros_manager.hpp"
#include "utils/Logger.hpp"

ROSManager& ROSManager::getInstance() {
    static ROSManager instance;
    return instance;
}

void ROSManager::init(int argc, char const *argv[]) {
    // std::call_once 保证即使多个线程同时调用 init()，内部逻辑也只执行一次
    std::call_once(init_flag_, [this, argc, argv]() {
        // 初始化ROS2
        rclcpp::init(argc, argv);

        // 创建节点
        node_ = std::make_shared<rclcpp::Node>("robot_avvtn_node");

        // 创建发布器（此时尚未设置 initialized_，无需加锁）
        wakeup_detail_publisher_ = node_->create_publisher<std_msgs::msg::String>("avvtn_wake", 10);

        // 语音唤醒发布器（无人服务厅）
        voice_wakeup_publisher_ = node_->create_publisher<std_msgs::msg::String>("voice_wake_topic", 10);

        // 语音业务指令发布器
        voice_topic_publisher_ = node_->create_publisher<std_msgs::msg::String>("voice_topic", 10);

        // 对话记录发布器
        chat_conversation_publisher_ = node_->create_publisher<std_msgs::msg::String>("chat_history", 10);

        // 创建ROS spin线程
        ros_spin_thread_ = std::thread([this]() {
            LOG_INFO("ROS2回调线程启动");
            rclcpp::spin(node_);
            LOG_INFO("ROS2回调线程结束");
        });

        // 最后设置标志，保证其他线程看到 initialized_==true 时，所有资源已就绪
        initialized_.store(true, std::memory_order_release);
        LOG_INFO("ROS管理器初始化成功");
    });
}

std::shared_ptr<rclcpp::Node> ROSManager::getNode() {
    return node_;
}

void ROSManager::publishMessage(const rclcpp::Publisher<std_msgs::msg::String>::SharedPtr& publisher,
                                const std::string& msg) {
    if (!initialized_.load(std::memory_order_acquire)) return;

    // 读锁：多个线程可以同时发布，不会互相阻塞
    // 仅当 shutdown() 持有写锁时才会等待
    std::shared_lock<std::shared_mutex> lock(pub_mutex_);

    // double-check：在获得锁之后再次检查，防止在等锁期间被 shutdown
    if (!initialized_.load(std::memory_order_acquire)) return;

    auto message = std_msgs::msg::String();
    message.data = msg;
    publisher->publish(message);
}

void ROSManager::publishWakeupDetail(const std::string& status_msg) {
    publishMessage(wakeup_detail_publisher_, status_msg);
}

void ROSManager::publishVoiceWakeup(const std::string& msg) {
    publishMessage(voice_wakeup_publisher_, msg);
}

void ROSManager::publishVoiceTopic(const std::string& msg) {
    publishMessage(voice_topic_publisher_, msg);
}

void ROSManager::publishChatConversation(const std::string& msg) {
    publishMessage(chat_conversation_publisher_, msg);
}

void ROSManager::subscribeTopic(const std::string& topic_name,
                               std::function<void(const std_msgs::msg::String::SharedPtr)> callback) {
    if (!initialized_.load(std::memory_order_acquire)) return;

    // 互斥锁：保护 subscribers_ map 的读写
    std::lock_guard<std::mutex> lock(sub_mutex_);

    // 检查是否已经订阅了该话题
    if (subscribers_.find(topic_name) != subscribers_.end()) {
        LOG_WARN("已经订阅了话题: {}", topic_name);
        return;
    }

    // 创建订阅者并保存
    auto subscriber = node_->create_subscription<std_msgs::msg::String>(
        topic_name, 10, callback);
    
    // 保存到map中，确保订阅者不被销毁
    subscribers_[topic_name] = subscriber;
    
    LOG_INFO("已订阅话题: {%s}", topic_name.c_str());
    LOG_INFO("当前订阅话题数量: {%zu}", subscribers_.size());
}

void ROSManager::shutdown() {
    if (!initialized_.load(std::memory_order_acquire)) return;

    // 1. 先将 initialized_ 置 false，阻止新的 publish/subscribe 调用进入
    initialized_.store(false, std::memory_order_release);

    // 2. 获取写锁，等待所有正在进行的 publish 调用完成
    {
        std::unique_lock<std::shared_mutex> pub_lock(pub_mutex_);
        // 写锁持有期间，所有 publishMessage() 的 shared_lock 都会等待
        // 此处不做额外操作，仅用来等待正在进行的发布完成
    }

    // 3. 清理订阅者
    {
        std::lock_guard<std::mutex> sub_lock(sub_mutex_);
        subscribers_.clear();
    }

    // 4. 关闭 ROS2（会使 rclcpp::spin 返回）
    rclcpp::shutdown();

    // 5. 等待 spin 线程结束
    //    注意：不能在 ROS 回调线程中调用 shutdown()，否则这里会死锁
    if (ros_spin_thread_.joinable()) {
        ros_spin_thread_.join();
    }

    LOG_INFO("ROS管理器已关闭");
}
