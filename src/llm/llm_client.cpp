#include "llm_client.h"
#include "utils/Logger.hpp"

#include <curl/curl.h>
#include <sstream>
#include <algorithm>

// JSON 转义辅助
static std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 10);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    return out;
}

// libcurl 写入回调
struct CurlWriteData {
    std::string* buffer;
    LlmStreamCallback callback;
    std::string line_buffer;
    std::atomic<bool>* cancel_flag;
};

static size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* data = static_cast<CurlWriteData*>(userdata);
    size_t total = size * nmemb;

    if (!data || !ptr || total == 0) return total;

    // 如果请求取消，返回 0 中止传输
    if (data->cancel_flag && *data->cancel_flag) {
        return 0;  // CURLE_WRITE_ERROR
    }

    data->line_buffer.append(ptr, total);

    // 按行处理 SSE
    size_t pos = 0;
    while ((pos = data->line_buffer.find('\n')) != std::string::npos) {
        std::string line = data->line_buffer.substr(0, pos);
        data->line_buffer.erase(0, pos + 1);

        // 去除 \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) continue;

        // 解析 SSE 数据
        if (line.substr(0, 6) == "data: ") {
            std::string json_data = line.substr(6);

            if (json_data == "[DONE]") {
                if (data->callback) {
                    data->callback("", true);
                }
                continue;
            }

            // 简单解析 content delta
            // 格式: {"choices":[{"delta":{"content":"..."}}]}
            std::string content;
            size_t content_pos = json_data.find("\"content\":\"");
            if (content_pos != std::string::npos) {
                size_t start = content_pos + 11;
                // 找到未转义的结束引号（前面没有反斜杠的 "）
                size_t end = start;
                while (end < json_data.size()) {
                    if (json_data[end] == '"' && (end == start || json_data[end - 1] != '\\')) {
                        break;
                    }
                    ++end;
                }
                if (end < json_data.size()) {
                    content = json_data.substr(start, end - start);
                    // 处理转义
                    std::string unescaped;
                    for (size_t i = 0; i < content.size(); ++i) {
                        if (content[i] == '\\' && i + 1 < content.size()) {
                            char next = content[i + 1];
                            if (next == 'n') { unescaped += '\n'; ++i; }
                            else if (next == 't') { unescaped += '\t'; ++i; }
                            else if (next == '"') { unescaped += '"'; ++i; }
                            else if (next == '\\') { unescaped += '\\'; ++i; }
                            else { unescaped += content[i]; }
                        } else {
                            unescaped += content[i];
                        }
                    }
                    content = unescaped;
                }
            }

            if (!content.empty()) {
                if (data->callback) {
                    if (!data->callback(content, false)) {
                        *data->cancel_flag = true;
                        return 0;
                    }
                }
                if (data->buffer) {
                    *data->buffer += content;
                }
            }
        }
    }

    return total;
}

LlmClient::~LlmClient() {
    Cancel();
    if (async_thread_.joinable()) {
        async_thread_.join();
    }
}

int LlmClient::Init(const std::string& base_url, const std::string& model) {
    base_url_ = base_url;
    // 移除末尾斜杠
    while (!base_url_.empty() && base_url_.back() == '/') {
        base_url_.pop_back();
    }
    model_ = model;

    // 初始化 curl（全局）
    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = true;
    }

    LOG_INFO("LLM 客户端初始化: %s", base_url_.c_str());
    return 0;
}

std::string LlmClient::Chat(const std::string& user_message, LlmStreamCallback callback) {
    if (base_url_.empty()) {
        LOG_ERROR("LLM 客户端未初始化");
        return "";
    }

    generating_ = true;
    cancel_requested_ = false;

    std::string response = DoRequest(user_message, callback);

    generating_ = false;

    // 保存到历史
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        history_.push_back({"user", user_message});
        history_.push_back({"assistant", response});
    }

    return response;
}

void LlmClient::ChatAsync(const std::string& user_message,
                          LlmStreamCallback callback,
                          std::function<void(const std::string&)> on_complete) {
    // 取消之前的请求
    Cancel();

    if (async_thread_.joinable()) {
        async_thread_.join();
    }

    async_thread_ = std::thread([this, user_message, callback, on_complete]() {
        std::string response = Chat(user_message, callback);
        if (on_complete) {
            on_complete(response);
        }
    });
}

void LlmClient::Cancel() {
    cancel_requested_ = true;
    // 等待异步线程结束
    if (async_thread_.joinable() && async_thread_.get_id() != std::this_thread::get_id()) {
        async_thread_.join();
    }
}

void LlmClient::ClearHistory() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    history_.clear();
}

void LlmClient::SetSystemPrompt(const std::string& system_prompt) {
    system_prompt_ = system_prompt;
}

void LlmClient::SetParams(float temperature, int max_tokens) {
    temperature_ = temperature;
    max_tokens_ = max_tokens;
}

std::string LlmClient::BuildRequestBody(const std::string& user_message) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"messages\":[";

    // System prompt
    if (!system_prompt_.empty()) {
        oss << "{\"role\":\"system\",\"content\":\"" << JsonEscape(system_prompt_) << "\"},";
    }

    // 历史消息（最近 10 条）
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        size_t start = history_.size() > 10 ? history_.size() - 10 : 0;
        for (size_t i = start; i < history_.size(); ++i) {
            oss << "{\"role\":\"" << history_[i].role
                << "\",\"content\":\"" << JsonEscape(history_[i].content) << "\"},";
        }
    }

    // 当前用户消息
    oss << "{\"role\":\"user\",\"content\":\"" << JsonEscape(user_message) << "\"}";
    oss << "],";

    if (!model_.empty()) {
        oss << "\"model\":\"" << JsonEscape(model_) << "\",";
    }

    oss << "\"temperature\":" << temperature_ << ",";
    oss << "\"max_tokens\":" << max_tokens_ << ",";
    oss << "\"stream\":true";
    oss << "}";

    return oss.str();
}

std::string LlmClient::DoRequest(const std::string& user_message, LlmStreamCallback callback) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("创建 curl 实例失败");
        return "";
    }

    std::string url = base_url_ + "/v1/chat/completions";
    std::string body = BuildRequestBody(user_message);
    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    CurlWriteData write_data;
    write_data.buffer = &response;
    write_data.callback = callback;
    write_data.cancel_flag = &cancel_requested_;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);  // 60 秒超时
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    LOG_INFO("LLM 请求: %s", user_message.c_str());
    LOG_DEBUG("LLM body: %s", body.c_str());

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK && res != CURLE_WRITE_ERROR) {
        LOG_ERROR("LLM 请求失败: %s", curl_easy_strerror(res));
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return response;
}

void LlmClient::ParseSSELine(const std::string& line, std::string& out_content, bool& out_done) {
    out_content.clear();
    out_done = false;

    if (line.substr(0, 6) != "data: ") return;

    std::string data = line.substr(6);
    if (data == "[DONE]") {
        out_done = true;
        return;
    }

    // 简单解析 content
    size_t pos = data.find("\"content\":\"");
    if (pos != std::string::npos) {
        size_t start = pos + 11;
        size_t end = data.find("\"", start);
        if (end != std::string::npos) {
            out_content = data.substr(start, end - start);
        }
    }
}
