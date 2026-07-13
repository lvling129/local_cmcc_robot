#ifndef REMOTE_LLM_CLIENT_H
#define REMOTE_LLM_CLIENT_H

#include "llm_client.h"

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

/**
 * @brief 远程闲聊 LLM 客户端
 *
 * 对接远程 OpenAI 兼容接口（/agent/llm/openai），处理其非标准 SSE 格式：
 * - 双 data: 前缀 (data:data: {...})
 * - finish_reason:"stop" 结束信号
 * - 更长超时（120s 总超时 / 15s 连接超时）
 *
 * 公共接口与 LlmClient 保持一致，可在 AvvtnCapture 中通过宏切换。
 */
class RemoteLlmClient {
public:
    RemoteLlmClient() = default;
    ~RemoteLlmClient();

    /**
     * @brief 初始化远程 LLM 客户端
     * @param url 完整请求 URL，如 "http://10.33.225.63:9090/agent/llm/openai"
     * @param model 模型名称，默认 "qwen"
     * @return 0 成功
     */
    int Init(const std::string& url, const std::string& model = "qwen");

    std::string Chat(const std::string& user_message, LlmStreamCallback callback = nullptr);

    void ChatAsync(const std::string& user_message,
                   LlmStreamCallback callback,
                   std::function<void(const std::string&)> on_complete = nullptr);

    void Cancel();
    void ClearHistory();
    void SetSystemPrompt(const std::string& system_prompt);
    void SetParams(float temperature = 0.7f, int max_tokens = 2048);

    bool IsGenerating() const { return generating_.load(); }

private:
    std::string DoRequest(const std::string& user_message, LlmStreamCallback callback);
    std::string BuildRequestBody(const std::string& user_message);

    std::string url_;
    std::string model_;
    std::string system_prompt_;
    float temperature_ = 0.7f;
    int max_tokens_ = 2048;

    std::vector<ChatMessage> history_;
    std::mutex history_mutex_;

    std::atomic<bool> generating_{false};
    std::atomic<bool> cancel_requested_{false};
    std::thread async_thread_;
};

#endif // REMOTE_LLM_CLIENT_H
