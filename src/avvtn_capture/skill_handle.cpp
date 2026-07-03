#include "avvtn_capture.h"
#include "utils/Logger.hpp"
#include "ros2/ros_manager.hpp"
#include "utils/json.hpp"
#include <curl/curl.h>
#include <string>
#include <iostream>
#include <memory>
#include <thread>

// 回调函数，用于接收响应数据
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// 简单的POST请求函数
bool postRequest(const std::string& endpoint, const nlohmann::json& body, 
                 const std::string& base_url = "http://192.168.123.164:8848") {
    // 初始化CURL
    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        curl_initialized = true;
    }
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL" << std::endl;
        return false;
    }
    
    // 构建完整URL
    std::string url = base_url + endpoint;
    
    // 将JSON转换为字符串
    std::string json_str = body.dump();
    
    // 响应数据
    std::string response;
    long http_code = 0;
    
    // 设置CURL选项
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_str.length());
    
    // 设置HTTP头
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // 设置超时（10秒）
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    // 设置回调函数接收响应
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    // 禁用SSL验证（仅用于测试环境）
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    // 记录请求日志
    std::cout << "请求URL: " << url << "; 请求body: " << json_str << std::endl;
    
    // 执行请求
    CURLcode res = curl_easy_perform(curl);
    
    // 获取HTTP状态码
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    }
    
    // 清理
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    // 检查结果
    if (res != CURLE_OK) {
        std::cerr << "请求失败: " << curl_easy_strerror(res) << std::endl;
        return false;
    }
    
    // 记录响应日志
    std::cout << "请求成功, 状态码: " << http_code 
              << ", 响应内容: " << response << std::endl;
    
    return true;
}

// 使用示例
void sendMoveRequest(const std::string& intent_name, const std::string& id,
                     const std::string& distance, const std::string& move_unit) {
    // 判断 distance 或 move_unit 是否为空，若为空则赋默认值
    std::string distance_fixed = distance.empty() ? "1" : distance;
    std::string move_unit_fixed = move_unit.empty() ? "step" : move_unit;
    // 构建请求体
    nlohmann::json request_body = {
        {"data", {
            {"intent_name", intent_name},
            {"id", id},
            {"distance", distance_fixed},
            {"move_unit", move_unit_fixed}
        }}
    };

    // 发送请求
    bool success = postRequest("/webrtc/move", request_body);
    
    if (!success) {
        std::cerr << "发送移动请求失败" << std::endl;
    }
}

// 发送转向请求
void sendTurnRequest(const std::string& intent_name, const std::string& id,
                     const std::string& distance, const std::string& turn_unit) {
    // 判断 distance 或 move_unit 是否为空，若为空则赋默认值
    std::string distance_fixed = distance.empty() ? "90" : distance;
    std::string turn_unit_fixed = turn_unit.empty() ? "degree" : turn_unit;
    // 构建请求体
    nlohmann::json request_body = {
        {"data", {
            {"intent_name", intent_name},
            {"id", id},
            {"distance", distance_fixed},
            {"turn_unit", turn_unit_fixed}
        }}
    };

    // 发送请求
    bool success = postRequest("/webrtc/turn", request_body);
    
    if (!success) {
        std::cerr << "发送转向请求失败" << std::endl;
    }
}

// 发送动作请求
void sendActionRequest(const std::string& intent_name, const std::string& id)
{
    // 构建请求体
    nlohmann::json request_body = {
        {"data", {
            {"intent_name", intent_name},
            {"id", id}
        }}
    };

    // 发送请求
    bool success = postRequest("/webrtc/action", request_body);
    
    if (!success) {
        std::cerr << "发送动作请求失败" << std::endl;
    }
}

// 发送停止请求
void sendStopRequest()
{
    // 构建请求体
    nlohmann::json request_body = {
        {"data", {}}
    };

    // 发送请求
    bool success = postRequest("/webrtc/stop", request_body);

    if (!success) {
        std::cerr << "发送停止请求失败" << std::endl;
    }
}


void AvvtnCapture::handleSkill(const std::string& text_str)
{
    try {
        // 解析text字段中的JSON
        auto text_root = nlohmann::json::parse(text_str);

        if (text_root.contains("category") && text_root["category"].is_string()) {
            std::string category = text_root["category"].get<std::string>();
            if (category == "IFLYTEK.datetimePro")
            {
                /*官方时间技能*/
                return;
            }
        }

        // 检查voice_answer content字段是否存在
        // 如果存在，播放技能返回的语音
        if (text_root.contains("voice_answer") &&
            text_root["voice_answer"].is_array() &&
            text_root["voice_answer"].size() > 0 &&
            text_root["voice_answer"][0].contains("content") &&
            text_root["voice_answer"][0]["content"].is_string() &&
            text_root["voice_answer"][0].contains("type") &&
            text_root["voice_answer"][0]["type"] == "TTS")

        {
            std::string voice_answer_content = text_root["voice_answer"][0]["content"].get<std::string>();
            LOG_INFO("技能返回TTS内容文本: %s", voice_answer_content.c_str());
            // 设置ignore本次大模型返回的NLP TTS语音
            ignore_tts_sid_ = current_iat_sid_;
            // 调用语音合成TTS，播放技能返回的语音文本
            aiui_wrapper_.StartTTS(voice_answer_content);
        }

        // 检查type字段是否存在
        if (text_root.contains("data") &&
            text_root["data"].contains("result") &&
            text_root["data"]["result"].is_array() &&
            text_root["data"]["result"].size() > 0 &&
            text_root["data"]["result"][0].contains("type") &&
            text_root["data"]["result"][0]["type"].is_string())

        {
            LOG_INFO("捕获到技能type!!!");
            std::string type = text_root["data"]["result"][0]["type"].get<std::string>();

            if (type == "face_rec_start")
            {
                LOG_INFO("执行调用人脸识别服务意图!!!");
            }
            else if (type == "move")
            {
                LOG_INFO("执行移动意图!!!");

                // 提取移动参数
                auto& result = text_root["data"]["result"][0];
                std::string intent_name = result["intentName"].get<std::string>();
                int id = result["id"].get<std::int32_t>();

                // 提取slots
                std::string distance;
                std::string move_unit;

                if (result.contains("slots") && result["slots"].is_object()) {
                    auto& slots = result["slots"];
                    
                    if (slots.contains("distance") && 
                        slots["distance"].is_object() &&
                        slots["distance"].contains("normValue") &&
                        slots["distance"]["normValue"].is_string())
                    {
                        distance = slots["distance"]["normValue"].get<std::string>();
                    }

                    if (slots.contains("move_unit") && 
                        slots["move_unit"].is_object() &&
                        slots["move_unit"].contains("normValue") &&
                        slots["move_unit"]["normValue"].is_string())
                    {
                        move_unit = slots["move_unit"]["normValue"].get<std::string>();
                    }
                }
                
                LOG_INFO("移动参数: intent_name=%s, id=%d, distance=%s, move_unit=%s",
                        intent_name.c_str(), id, distance.c_str(), move_unit.c_str());
                
                // TODO: 发送移动请求
                sendMoveRequest(intent_name, std::to_string(id), distance, move_unit);
            }
            else if (type == "turn")
            {
                LOG_INFO("执行转向意图!!!");
                // 提取移动参数
                auto& result = text_root["data"]["result"][0];
                std::string intent_name = result["intentName"].get<std::string>();
                int id = result["id"].get<std::int32_t>();

                // 提取slots
                std::string distance;
                std::string turn_unit;

                if (result.contains("slots") && result["slots"].is_object()) {
                    auto& slots = result["slots"];
                    
                    if (slots.contains("distance") && 
                        slots["distance"].is_object() &&
                        slots["distance"].contains("normValue") &&
                        slots["distance"]["normValue"].is_string())
                    {
                        distance = slots["distance"]["normValue"].get<std::string>();
                    }

                    if (slots.contains("turn_unit") && 
                        slots["turn_unit"].is_object() &&
                        slots["turn_unit"].contains("normValue") &&
                        slots["turn_unit"]["normValue"].is_string())
                    {
                        turn_unit = slots["turn_unit"]["normValue"].get<std::string>();
                    }
                }
                
                LOG_INFO("移动参数: intent_name=%s, id=%d, distance=%s, turn_unit=%s",
                        intent_name.c_str(), id, distance.c_str(), turn_unit.c_str());
                
                // TODO: 发送移动请求
                sendTurnRequest(intent_name, std::to_string(id), distance, turn_unit);
            }
            else if (type == "action")
            {
                LOG_INFO("执行动作意图!!!");
                // 提取动作参数
                auto& result = text_root["data"]["result"][0];
                std::string intent_name = result["intentName"].get<std::string>();
                int id = result["id"].get<std::int32_t>();

                LOG_INFO("动作参数: intent_name=%s, id=%d", intent_name.c_str(), id);
                
                // 发送动作请求
                sendActionRequest(intent_name, std::to_string(id));
            }
            else if (type == "stop")
            {
                LOG_INFO("执行停止意图!!!");

                // TODO: 发送移动请求
                sendStopRequest();
            }
            else if (type == "shut_up")
            {
                LOG_INFO("执行停止语音交互意图!!!");

                // 播放shut_up回应语音
                is_playing = true;
                // 发送SLEEP给AIUI，重置状态到等待唤醒
                aiui_wrapper_.ResetWakeup();

            }
            else if (type == "vip")
            {
                LOG_INFO("执行 VIP 场景意图!!!");

                // 提取vip参数
                auto& result = text_root["data"]["result"][0];
                std::string intent_name = result["intentName"].get<std::string>();


            }
            else if (type == "new_year")
            {
                LOG_INFO("执行新年动作意图!!!");
                // 提取动作参数
                auto& result = text_root["data"]["result"][0];
                std::string intent_name = result["intentName"].get<std::string>();

                int ids[6] = {2003, 2014, 2016, 3052, 3053, 3012};
                int id = ids[rand() % 6];
                LOG_INFO("动作参数: intent_name=%s, id=%d", intent_name.c_str(), id);

                // 发送动作请求
                sendActionRequest(intent_name, std::to_string(id));
            }
            else if (type == "phone_number_inquiry")
            {
                LOG_INFO("根据名字查询电话号码");
                // 提取动作参数
                auto& result = text_root["data"]["result"][0];
                std::string intent_name = result["intentName"].get<std::string>();
                std::string customer_name = result["customerName"].get<std::string>();

                LOG_INFO("查询电话号码: customer_name=%s", customer_name.c_str());

                // 异步发送HTTP GET请求查询电话号码
                std::thread([customer_name]() {
                    CURL* curl = curl_easy_init();
                    if (!curl) {
                        LOG_ERROR("初始化CURL失败");
                        return;
                    }
                    std::string url;
                    char* encoded_name = curl_easy_escape(curl, customer_name.c_str(), customer_name.size());
                    if (encoded_name) {
                        url = std::string("http://192.168.16.26:8086/rpa?name=") + encoded_name;
                        curl_free(encoded_name);
                    } else {
                        url = "http://192.168.16.26:8086/rpa?name=" + customer_name;
                    }
                    std::string response;
                    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
                    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
                    CURLcode res = curl_easy_perform(curl);
                    if (res != CURLE_OK) {
                        LOG_ERROR("HTTP GET请求失败: %s, url: %s", curl_easy_strerror(res), url.c_str());
                    } else {
                        LOG_INFO("HTTP GET响应: %s", response.c_str());
                    }
                    curl_easy_cleanup(curl);
                }).detach();
            }
            else
            {
                LOG_INFO("新的未定义技能！！！");
            }
        }

    } catch (const nlohmann::json::parse_error& e) {
        LOG_ERROR("解析text字段失败: %s, text_str: %s", e.what(), text_str.c_str());
        return;
    } catch (const nlohmann::json::type_error& e) {
        LOG_ERROR("类型错误: %s", e.what());
        return;
    }

}