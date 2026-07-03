#include "Logger.hpp"
#include "ros2/ros_manager.hpp"

namespace Logger {

// 静态成员变量定义（只有一个.cpp文件包含这些定义）
std::shared_ptr<Logger> Logger::instance_ = nullptr;
std::mutex Logger::instanceMutex_;

// 构造函数定义
Logger::Logger() : level_(LogLevel::LOG_INFO), asyncMode_(false) {
    formatter_ = std::unique_ptr<DefaultFormatter>(new DefaultFormatter());
    // 默认不添加控制台输出
    //AddSink(std::unique_ptr<ConsoleSink>(new ConsoleSink(true)));
}

// 获取单例实例
Logger& Logger::GetInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = std::shared_ptr<Logger>(new Logger());
    }
    return *instance_;
}

// 设置日志级别
void Logger::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::GetLevel() const {
    return level_;
}

// 添加输出目标
void Logger::AddSink(std::unique_ptr<LogSink> sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (asyncMode_) {
        sinks_.push_back(std::unique_ptr<AsyncSink>(new AsyncSink(std::move(sink))));
    } else {
        sinks_.push_back(std::move(sink));
    }
}

// 设置异步模式
void Logger::SetAsyncMode(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (asyncMode_ != enable) {
        asyncMode_ = enable;
        // 重新创建sinks（实际应用中需要更复杂的处理）
        // 这里简化处理，建议在添加所有sink前设置async模式
    }
}

// 设置格式化器
void Logger::SetFormatter(std::unique_ptr<LogFormatter> formatter) {
    std::lock_guard<std::mutex> lock(mutex_);
    formatter_ = std::move(formatter);
}

// 日志记录主函数
void Logger::Log(LogLevel level, const std::string& message,
                 const std::string& file, int line) {
    if (static_cast<int>(level) < static_cast<int>(level_)) return;

    LogEntry entry(level, message, file, line);
    std::string formatted;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        formatted = formatter_->Format(entry);
    }

    // 遍历所有输出目标
    for (auto& sink : sinks_) {
        sink->Write(entry, formatted);
    }
}

// 刷新所有输出
void Logger::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& sink : sinks_) {
        sink->Flush();
    }
}

} // namespace Logger