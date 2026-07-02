#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

/**
 * @brief 聊天消息
 */
struct ChatMessage {
    std::string role;     // "system", "user", "assistant"
    std::string content;
};

/**
 * @brief LLM 流式响应回调
 * @param chunk 新生成的文本片段
 * @param is_done 是否生成完成
 * @return true 继续接收，false 取消
 */
using LlmStreamCallback = std::function<bool(const std::string& chunk, bool is_done)>;

/**
 * @brief llama-server HTTP 客户端
 *
 * 支持：
 * - OpenAI 兼容 API (/v1/chat/completions)
 * - 流式响应 (SSE)
 * - 异步非阻塞调用
 * - 对话历史管理
 *
 * 使用方法：
 * 1. Init() 配置服务器地址
 * 2. Chat() 发送消息，通过回调接收流式响应
 * 3. ChatAsync() 异步发送，不阻塞当前线程
 */
class LlmClient {
public:
    LlmClient() = default;
    ~LlmClient();

    /**
     * @brief 初始化 LLM 客户端
     * @param base_url 服务器地址，如 "http://localhost:8080"
     * @param model 模型名称（可选）
     * @return 0 成功
     */
    int Init(const std::string& base_url, const std::string& model = "");

    /**
     * @brief 同步对话（阻塞直到完成）
     * @param user_message 用户输入
     * @param callback 流式回调，可为 nullptr
     * @return 完整回复文本
     */
    std::string Chat(const std::string& user_message, LlmStreamCallback callback = nullptr);

    /**
     * @brief 异步对话（非阻塞）
     * @param user_message 用户输入
     * @param callback 流式回调
     * @param on_complete 完成回调（可选）
     */
    void ChatAsync(const std::string& user_message,
                   LlmStreamCallback callback,
                   std::function<void(const std::string&)> on_complete = nullptr);

    /**
     * @brief 取消当前正在进行的请求
     */
    void Cancel();

    /**
     * @brief 清空对话历史
     */
    void ClearHistory();

    /**
     * @brief 设置系统提示词
     * @param system_prompt 系统提示词
     */
    void SetSystemPrompt(const std::string& system_prompt);

    /**
     * @brief 设置生成参数
     * @param temperature 温度 (0-2)，默认 0.7
     * @param max_tokens 最大 token 数，默认 512
     */
    void SetParams(float temperature = 0.7f, int max_tokens = 512);

    /**
     * @brief 检查是否正在生成
     */
    bool IsGenerating() const { return generating_.load(); }

private:
    std::string DoRequest(const std::string& user_message, LlmStreamCallback callback);
    std::string BuildRequestBody(const std::string& user_message);
    void ParseSSELine(const std::string& line, std::string& out_content, bool& out_done);

    std::string base_url_;
    std::string model_;
    std::string system_prompt_;
    float temperature_ = 0.7f;
    int max_tokens_ = 512;

    std::vector<ChatMessage> history_;
    std::mutex history_mutex_;

    std::atomic<bool> generating_{false};
    std::atomic<bool> cancel_requested_{false};
    std::thread async_thread_;
};

#endif // LLM_CLIENT_H
