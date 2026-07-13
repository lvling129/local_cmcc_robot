#include "remote_llm_client.h"
#include "utils/Logger.hpp"

#include <curl/curl.h>
#include <sstream>
#include <algorithm>
#include <chrono>

// JSON 转义辅助（与 llm_client.cpp 共用同一逻辑，但独立定义避免链接依赖）
static std::string RemoteJsonEscape(const std::string& s) {
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

// libcurl 写入回调（处理双 data: 前缀 + finish_reason 结束）
struct RemoteCurlWriteData {
    std::string* buffer;
    LlmStreamCallback callback;
    std::string line_buffer;
    std::atomic<bool>* cancel_flag;
    bool done_sent = false;
    bool is_error_event = false;   // 追踪 event:error 类型
    std::string* error_msg = nullptr;  // 存储错误信息
};

static size_t RemoteCurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* data = static_cast<RemoteCurlWriteData*>(userdata);
    size_t total = size * nmemb;

    if (!data || !ptr || total == 0) return total;

    if (data->cancel_flag && *data->cancel_flag) {
        return 0;
    }

    data->line_buffer.append(ptr, total);

    size_t pos = 0;
    while ((pos = data->line_buffer.find('\n')) != std::string::npos) {
        std::string line = data->line_buffer.substr(0, pos);
        data->line_buffer.erase(0, pos + 1);

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) continue;

        // 检测 event: 行（服务端错误格式：event:error 或 event: error）
        if (line.size() >= 6 && line.substr(0, 6) == "event:") {
            std::string event_type = line.substr(6);
            // 去掉前导空格
            if (!event_type.empty() && event_type[0] == ' ') {
                event_type = event_type.substr(1);
            }
            data->is_error_event = (event_type == "error");
            continue;
        }

        // 解析 SSE 数据 — 兼容 "data: xxx" 和 "data:xxx"（有无空格）
        std::string json_data;
        if (line.size() >= 6 && line.substr(0, 5) == "data:") {
            json_data = line.substr(5);
            // 去掉 data: 后的前导空格
            if (!json_data.empty() && json_data[0] == ' ') {
                json_data = json_data.substr(1);
            }
        } else {
            continue;  // 跳过非 data 行
        }

        // 处理 error event 的 data 行
        if (data->is_error_event) {
            // 提取错误信息: {"code":50000,"message":"..."}
            std::string err_info = json_data;
            size_t msg_pos = err_info.find("\"message\":\"");
            if (msg_pos != std::string::npos) {
                size_t start = msg_pos + 11;
                size_t end = err_info.find("\"", start);
                if (end != std::string::npos) {
                    err_info = err_info.substr(start, end - start);
                }
            }
            if (data->error_msg) {
                *data->error_msg = err_info;
            }
            if (data->callback && !data->done_sent) {
                data->done_sent = true;
                data->callback("", true);  // 通知结束
            }
            data->is_error_event = false;
            continue;
        }

        // 兼容双 data: 前缀 (data:data: {...} 或 data:data:{...})
        if (json_data.size() >= 5 && json_data.substr(0, 5) == "data:") {
            json_data = json_data.substr(5);
            if (!json_data.empty() && json_data[0] == ' ') {
                json_data = json_data.substr(1);
            }
        }

        // [DONE] 结束
        if (json_data == "[DONE]") {
            if (data->callback && !data->done_sent) {
                data->done_sent = true;
                data->callback("", true);
            }
            continue;
        }

        // 检测 finish_reason:"stop"
        bool is_finish = (json_data.find("\"finish_reason\":\"stop\"") != std::string::npos ||
                          json_data.find("\"finish_reason\": \"stop\"") != std::string::npos);

        // 解析 content delta
        std::string content;
        size_t content_pos = json_data.find("\"content\":\"");
        if (content_pos != std::string::npos) {
            size_t start = content_pos + 11;
            size_t end = start;
            while (end < json_data.size()) {
                if (json_data[end] == '"' && (end == start || json_data[end - 1] != '\\')) {
                    break;
                }
                ++end;
            }
            if (end < json_data.size()) {
                content = json_data.substr(start, end - start);
                // 处理转义字符
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

        // finish_reason=stop 结束（防重复）
        if (is_finish && data->callback && !data->done_sent) {
            data->done_sent = true;
            data->callback("", true);
        }
    }

    return total;
}

RemoteLlmClient::~RemoteLlmClient() {
    Cancel();
    if (async_thread_.joinable()) {
        async_thread_.join();
    }
}

int RemoteLlmClient::Init(const std::string& url, const std::string& model) {
    url_ = url;
    model_ = model;

    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = true;
    }

    LOG_INFO("远程 LLM 客户端初始化: %s (model=%s)", url_.c_str(), model_.c_str());
    return 0;
}

std::string RemoteLlmClient::Chat(const std::string& user_message, LlmStreamCallback callback) {
    if (url_.empty()) {
        LOG_ERROR("远程 LLM 客户端未初始化");
        return "";
    }

    generating_ = true;
    cancel_requested_ = false;

    std::string response = DoRequest(user_message, callback);

    generating_ = false;

    // 只在有实际回复时保存到历史
    if (!response.empty()) {
        std::lock_guard<std::mutex> lock(history_mutex_);
        history_.push_back({"user", user_message});
        history_.push_back({"assistant", response});
    }

    return response;
}

void RemoteLlmClient::ChatAsync(const std::string& user_message,
                                 LlmStreamCallback callback,
                                 std::function<void(const std::string&)> on_complete) {
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

void RemoteLlmClient::Cancel() {
    cancel_requested_ = true;
    if (async_thread_.joinable() && async_thread_.get_id() != std::this_thread::get_id()) {
        async_thread_.join();
    }
}

void RemoteLlmClient::ClearHistory() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    history_.clear();
}

void RemoteLlmClient::SetSystemPrompt(const std::string& system_prompt) {
    system_prompt_ = system_prompt;
}

void RemoteLlmClient::SetParams(float temperature, int max_tokens) {
    temperature_ = temperature;
    max_tokens_ = max_tokens;
}

std::string RemoteLlmClient::BuildRequestBody(const std::string& user_message) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"messages\":[";

    if (!system_prompt_.empty()) {
        oss << "{\"role\":\"system\",\"content\":\"" << RemoteJsonEscape(system_prompt_) << "\"},";
    }

    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        size_t start = history_.size() > 10 ? history_.size() - 10 : 0;
        for (size_t i = start; i < history_.size(); ++i) {
            // 跳过空消息（服务端可能无法处理）
            if (history_[i].content.empty()) continue;
            oss << "{\"role\":\"" << history_[i].role
                << "\",\"content\":\"" << RemoteJsonEscape(history_[i].content) << "\"},";
        }
    }

    oss << "{\"role\":\"user\",\"content\":\"" << RemoteJsonEscape(user_message) << "\"}";
    oss << "],";

    if (!model_.empty()) {
        oss << "\"model\":\"" << RemoteJsonEscape(model_) << "\",";
    }

    oss << "\"temperature\":" << temperature_ << ",";
    oss << "\"max_tokens\":" << max_tokens_ << ",";
    oss << "\"stream\":true";
    oss << "}";

    return oss.str();
}

std::string RemoteLlmClient::DoRequest(const std::string& user_message, LlmStreamCallback callback) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("创建 curl 实例失败");
        return "";
    }

    std::string body = BuildRequestBody(user_message);
    std::string response;

    // 只保留 Content-Type，与 curl 命令行行为一致
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    RemoteCurlWriteData write_data;
    write_data.buffer = &response;
    write_data.callback = callback;
    write_data.cancel_flag = &cancel_requested_;
    std::string error_msg;
    write_data.error_msg = &error_msg;

    curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, RemoteCurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_data);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    LOG_INFO("远程 LLM 请求: %s", user_message.c_str());

    CURLcode res = CURLE_OK;
    int max_retries = 3;

    for (int attempt = 1; attempt <= max_retries; ++attempt) {
        error_msg.clear();
        response.clear();

        res = curl_easy_perform(curl);

        if (!error_msg.empty()) {
            LOG_ERROR("远程 LLM 服务端错误(第%d次): %s", attempt, error_msg.c_str());
            if (attempt < max_retries) {
                LOG_INFO("远程 LLM 重试中...");
                write_data.done_sent = false;
                write_data.is_error_event = false;
                write_data.line_buffer.clear();
                error_msg.clear();
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        } else if (res != CURLE_OK && res != CURLE_WRITE_ERROR) {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            LOG_ERROR("远程 LLM 请求失败(第%d次): %s (HTTP %ld, 已收到 %zu 字节)",
                      attempt, curl_easy_strerror(res), http_code, response.size());
            if (attempt < max_retries) {
                LOG_INFO("远程 LLM 重试中...");
                write_data.done_sent = false;
                write_data.line_buffer.clear();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        } else if (response.empty() && res == CURLE_OK) {
            // curl 成功但解析到空响应（服务端可能返回了不可识别的格式）
            LOG_WARN("远程 LLM 响应为空(第%d次)，服务端可能返回了异常格式", attempt);
            if (attempt < max_retries) {
                LOG_INFO("远程 LLM 重试中...");
                write_data.done_sent = false;
                write_data.line_buffer.clear();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        }

        break;  // 成功或最后一次失败
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return response;
}
