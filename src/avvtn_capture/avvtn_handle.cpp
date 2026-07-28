#include "avvtn_capture/avvtn_capture.h"
#include "utils/Logger.hpp"
#include "utils/json.hpp"
#include "ros2/ros_manager.hpp"
#include <cstdlib>

int AvvtnCapture::test_evaluate_keyword(const char *keyword)
{
    int ret                                   = 0;
    char test_res_out[128]                    = { 0 };    // 必填
    avvtn_interact_info_t avvtn_interact_info = {};
    avvtn_interact_info.type                  = AVVTN_INTERACT_TYPE_EVALUATE_KEYWORD;
    avvtn_interact_info.with_out              = 0;
    avvtn_interact_info.in.raw                = (void *)keyword;
    avvtn_interact_info.in.raw_size           = strlen(keyword);
    avvtn_interact_info.out.str               = test_res_out;
    avvtn_interact_info.out.str_size          = sizeof(test_res_out);
    ret                                       = avvtn_api_interact(avvtn_cap_, &avvtn_interact_info);
    CHECK_RET(ret);
    std::cout << "evaluate keyword result: " << test_res_out << std::endl;
    return ret;
}

int AvvtnCapture::test_generate_keyword(const char *keyword, const char *output_path)
{
    char test_raw_out[4096]                   = { 0 };    // 必填
    avvtn_interact_info_t avvtn_interact_info = {};
    avvtn_interact_info.type                  = AVVTN_INTERACT_TYPE_GENERATE_KEYWORD;
    avvtn_interact_info.with_out              = 0;
    avvtn_interact_info.in.str                = (char *)keyword;
    avvtn_interact_info.in.str_size           = strlen(keyword);
    avvtn_interact_info.out.raw               = test_raw_out;
    avvtn_interact_info.out.raw_size          = sizeof(test_raw_out);
    int ret                                   = avvtn_api_interact(avvtn_cap_, &avvtn_interact_info);
    CHECK_RET(ret);
    FILE *fp = NULL;
    if (NULL == (fp = fopen(output_path, "wb")))
    {
        printf("failed on create custom_keyword: \"%s\"", output_path);
        return -1;
    }
    fwrite(avvtn_interact_info.out.raw, 1, avvtn_interact_info.out.raw_size, fp);
    fclose(fp);
    return ret;
}

int AvvtnCapture::test_add_keyword(const char *output_path)
{
    int ret                                   = 0;
    char test_res_out[128]                    = { 0 };    // 必填
    avvtn_interact_info_t avvtn_interact_info = {};
    avvtn_interact_info.type                  = AVVTN_INTERACT_TYPE_ADD_KEYWORD;
    avvtn_interact_info.with_out              = 0;
    avvtn_interact_info.in.raw                = (void *)output_path;
    avvtn_interact_info.in.raw_size           = sizeof(output_path);
    avvtn_interact_info.out.str               = test_res_out;
    avvtn_interact_info.out.str_size          = sizeof(test_res_out);
    ret                                       = avvtn_api_interact(avvtn_cap_, &avvtn_interact_info);
    CHECK_RET(ret);
    std::cout << "add keyword result: " << test_res_out << std::endl;
    return ret;
}

int AvvtnCapture::test_remove_keyword(const char *keywords_id)
{
    int ret                                   = 0;
    char test_res_out[128]                    = { 0 };    // 必填
    avvtn_interact_info_t avvtn_interact_info = {};
    avvtn_interact_info.type                  = AVVTN_INTERACT_TYPE_REMOVE_KEYWORD;
    avvtn_interact_info.with_out              = 0;
    avvtn_interact_info.in.str                = (char *)keywords_id;
    avvtn_interact_info.in.str_size           = strlen(keywords_id);
    avvtn_interact_info.out.str               = test_res_out;
    avvtn_interact_info.out.str_size          = sizeof(test_res_out);
    ret                                       = avvtn_api_interact(avvtn_cap_, &avvtn_interact_info);
    CHECK_RET(ret);
    std::cout << "remove keyword result: " << test_res_out << std::endl;
    return ret;
}

int AvvtnCapture::test_set_param(const char *param)
{
    int ret                                   = 0;
    avvtn_interact_info_t avvtn_interact_info = {};
    avvtn_interact_info.type                  = AVVTN_INTERACT_TYPE_SET_PARAM;
    avvtn_interact_info.with_out              = 0;
    avvtn_interact_info.in.str                = (char *)param;
    avvtn_interact_info.in.str_size           = strlen(param);
    ret                                       = avvtn_api_interact(avvtn_cap_, &avvtn_interact_info);
    CHECK_RET(ret);
    return ret;
}

int AvvtnCapture::test_set_beam(const char *beam_id)
{
    int ret                                   = 0;
    avvtn_interact_info_t avvtn_interact_info = {};
    avvtn_interact_info.type                  = AVVTN_INTERACT_TYPE_SET_BEAM;
    avvtn_interact_info.with_out              = 0;
    avvtn_interact_info.in.str                = (char *)beam_id;
    avvtn_interact_info.in.str_size           = strlen(beam_id);
    ret                                       = avvtn_api_interact(avvtn_cap_, &avvtn_interact_info);
    CHECK_RET(ret);
    return ret;
}

int AvvtnCapture::parseJsonAndCheckData(avvtn_callback_data_t *data_p, cJSON **json, cJSON **data)
{
    std::string param_str = std::string((char *)data_p->param, data_p->param_size);
    // std::cout << "param: " << param_str.c_str() << " len: " << data_p->param_size << std::endl;
    *json = cJSON_Parse(param_str.c_str());
    if (*json == nullptr)
    {
        LOG_ERROR("Failed to parse JSON!");
        LOG_ERROR("param: %s len: %d", param_str.c_str(), data_p->param_size);
        std::cerr << "Failed to parse JSON!" << std::endl;
        std::cerr << "param: " << param_str.c_str() << " len: " << data_p->param_size << std::endl;
        return -1;
    }

    *data = cJSON_GetObjectItem(*json, "data");
    if (*data == nullptr)
    {
        LOG_ERROR("No data object found");
        std::cerr << "No data object found" << std::endl;
        cJSON_Delete(*json);
        return -1;
    }

    return 0;
}

void AvvtnCapture::handleAudioCAE(avvtn_callback_data_t *data_p)
{
    LOG_TRACE("触发降噪音频回调");
    // data_p->param 是json格式，需要解析json数据，data_p->data 是音频数据 data_p->data_size 是音频数据大小
    // 降噪音频的通道数为 -1 0 1 2 分别代表纯声学 0说话人 1说话人 2说话人 vad_status 0 1 2 3 分别代表静音 开始说话 说话中 结束说话
    cJSON *json, *data;
    if (parseJsonAndCheckData(data_p, &json, &data) != 0)
    {
        return;
    }
    int channel        = -2;
    int vad_status     = -1;
    cJSON *channelItem = cJSON_GetObjectItem(data, "channel");
    if (channelItem != NULL && cJSON_IsNumber(channelItem))
    {
        channel = channelItem->valueint;
        LOG_TRACE("降噪音频回调: channel = %d", channel);
    }

    cJSON *vadStatus = cJSON_GetObjectItem(data, "vad_status");
    if (vadStatus != NULL && cJSON_IsNumber(vadStatus))
    {
        vad_status = vadStatus->valueint;
        LOG_TRACE("降噪音频回调: vad_status = %d", vad_status);
    }

// 保存音频文件
#if 0
    // 通道在-2到3之间，则认为是有效数据，否则认为不是有效数据
    if (channel > -2 && channel < 3)
    {
        static FILE *cae_audio_file  = NULL;
        static FILE *cae_audio_file1 = NULL;
        static FILE *cae_audio_file2 = NULL;
        static FILE *cae_audio_file3 = NULL;
        if (channel == -1)
        {
            if (cae_audio_file == NULL)
            {
                cae_audio_file = fopen("cae_audio_channel_ivw.pcm", "wb+");
            }
            fwrite(data_p->data, 1, data_p->data_size, cae_audio_file);
        }
        else if (channel == 0)
        {
            if (cae_audio_file1 == NULL)
            {
                cae_audio_file1 = fopen("cae_audio_channel_0.pcm", "wb+");
            }
            fwrite(data_p->data, 1, data_p->data_size, cae_audio_file1);
        }
        else if (channel == 1)
        {
            if (cae_audio_file2 == NULL)
            {
                cae_audio_file2 = fopen("cae_audio_channel_1.pcm", "wb+");
            }
            fwrite(data_p->data, 1, data_p->data_size, cae_audio_file2);
        }
        else if (channel == 2)
        {
            if (cae_audio_file3 == NULL)
            {
                cae_audio_file3 = fopen("cae_audio_channel_2.pcm", "wb+");
            }
            fwrite(data_p->data, 1, data_p->data_size, cae_audio_file3);
        }
}
#endif
    cJSON_Delete(json);
    return;
}

void AvvtnCapture::handleAudioRec(avvtn_callback_data_t *data_p)
{
    LOG_DEBUG("触发识别音频回调");
    cJSON *json, *data;
    if (parseJsonAndCheckData(data_p, &json, &data) != 0)
    {
        return;
    }

    int channel    = -1;
    int vad_status = -1;

    cJSON *channelItem = cJSON_GetObjectItem(data, "channel");
    if (channelItem != NULL && cJSON_IsNumber(channelItem))
    {
        channel = channelItem->valueint;
        LOG_DEBUG("识别音频回调: channel = %d", channel);
    }

    cJSON *vadStatus = cJSON_GetObjectItem(data, "vad_status");
    if (vadStatus != NULL && cJSON_IsNumber(vadStatus))
    {
        vad_status = vadStatus->valueint;
        LOG_DEBUG("识别音频回调: vad_status = %d", vad_status);
    }
    cJSON_Delete(json);

    if (!is_sleeping && sherpa_asr_.IsInitialized()) {
        if (vad_status == 3)
        {
            // VAD 结束（当前帧为空，无需 FeedAudio）
            // 将累积的整句音频一次性送入并解码
            sherpa_asr_.FinalizeAudio();

            // 获取最终识别结果并发布
            std::string final_text = sherpa_asr_.GetResult();
            if (!final_text.empty()) {
                LOG_INFO("SenseVoice 识别结果: %s", final_text.c_str());

                // 只处理中文 ASR 结果，其他语言跳过所有后续流程
                std::string lang = sherpa_asr_.GetLang();
                if (lang.find("zh") == std::string::npos) {
                    LOG_INFO("非中文识别结果(lang=%s)，跳过", lang.c_str());
                    sherpa_asr_.Reset();
                    LOG_INFO("VAD结束，重置 SenseVoice 识别状态");
                    return;
                }

                // 发布到 /chat_history 话题
                nlohmann::json chat_asr = {
                    {"speaker", "PERSON"},
                    {"content", final_text}
                };
                ROSManager::getInstance().publishChatConversation(chat_asr.dump());

                // 第一步：意图识别（同步，快速返回）
                std::string intent_json = llm_intent_.Chat(final_text);
                LOG_INFO("意图识别原始响应: [%s]", intent_json.c_str());

                // 解析意图和置信度
                std::string intent = "chat";
                std::string confidence = "high";

                // 提取 JSON 中 "key":"value" 的通用函数
                auto extract_field = [&](const std::string& key) -> std::string {
                    size_t pos = intent_json.find("\"" + key + "\"");
                    if (pos == std::string::npos) return "";
                    size_t colon = intent_json.find(":", pos);
                    size_t q1 = intent_json.find("\"", colon + 1);
                    size_t q2 = intent_json.find("\"", q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos) {
                        return intent_json.substr(q1 + 1, q2 - q1 - 1);
                    }
                    return "";
                };

                std::string parsed_intent = extract_field("intent");
                std::string parsed_confidence = extract_field("confidence");
                if (!parsed_intent.empty()) {
                    intent = parsed_intent;
                } else {
                    LOG_WARN("意图识别未返回有效JSON，默认闲聊");
                }
                if (!parsed_confidence.empty()) {
                    confidence = parsed_confidence;
                }
                LOG_INFO("解析意图: %s, 置信度: %s", intent.c_str(), confidence.c_str());

                // 低置信度兜底：视为闲聊，让对话模型处理
                if (confidence == "low") {
                    // LOG_INFO("置信度低，视为闲聊，调用对话模型");
                    LOG_INFO("置信度低，暂不处理，只记录日志");
                    // ChatAndSpeak(final_text);
                }
                // 闲聊意图
                else if (intent == "chat" ||
                         intent.find("chat") != std::string::npos ||
                         intent.find("闲聊") != std::string::npos) {
                    LOG_INFO("意图: 闲聊，调用对话模型");
                    ChatAndSpeak(final_text);
                }
                else {
                    // 业务指令：内部处理，不触发对话模型
                    LOG_INFO("意图: %s，业务处理", intent.c_str());

                    // 确定业务类型并发布到 /voice_topic
                    std::string business_type;
                    if (intent.find("balance") != std::string::npos || intent.find("bill") != std::string::npos) {
                        business_type = "query_balance";
                    } else if (intent.find("traffic") != std::string::npos) {
                        business_type = "query_traffic";
                    } else if (intent.find("package") != std::string::npos) {
                        business_type = "query_package";
                    } else if (intent.find("new_sim_card") != std::string::npos || intent.find("card") != std::string::npos) {
                        business_type = "new_sim_card";
                    } else if (intent.find("knowledge") != std::string::npos) {
                        business_type = "knowledge";
                        Speak("您的请求需要知识库支持，正在开发中。");
                    } else if (intent == "back_home" || intent.find("back_home") != std::string::npos) {
                        business_type = "back_home";
                    } else if (intent == "continue_current" || intent.find("continue") != std::string::npos) {
                        business_type = "continue_current";
                    } else {
                        business_type = intent;
                        Speak("好的，正在为您处理。");
                    }

                    // 发布业务指令到 /voice_topic
                    nlohmann::json voice_msg = {
                        {"business_type", business_type},
                        {"content", final_text}
                    };
                    ROSManager::getInstance().publishVoiceTopic(voice_msg.dump());
                    LOG_INFO("发布业务指令: %s", voice_msg.dump().c_str());
                }
            }
            // 重置识别状态，准备下一句
            sherpa_asr_.Reset();
            LOG_INFO("VAD结束，重置 SenseVoice 识别状态");
        }
        else if (data_p->data && data_p->data_size > 0)
        {
            // 正常音频，累积到 utterance_buffer_
            sherpa_asr_.FeedAudio(
                reinterpret_cast<const int16_t*>(data_p->data),
                data_p->data_size);
        }
    }

// 保存音频文件
#if 0
    static FILE *rec_audio_file;
    if (rec_audio_file == NULL)
    {
        rec_audio_file = fopen("rec_audio.pcm", "wb+");
    }
    fwrite(data_p->data, 1, data_p->data_size, rec_audio_file);
#endif
    return;
}

void AvvtnCapture::handleAudioWake(avvtn_callback_data_t *data_p)
{
    // 唤醒会触发两次handleAudioWake，先wakeup后wakeup_detail
    std::string wake_str = std::string((char *)data_p->data, data_p->data_size);
    LOG_INFO("AVVTN接收到唤醒语音: %s", wake_str.c_str());

    // 只处理 wakeup，跳过 wakeup_detail
    try {
        auto wake_json = nlohmann::json::parse(wake_str);
        std::string msg_type = wake_json.value("msg_type", "");
        if (msg_type == "wakeup_detail") {
            LOG_INFO("跳过 wakeup_detail 消息");
            return;
        }
    } catch (...) {
        LOG_WARN("唤醒消息JSON解析失败，按默认流程处理");
    }

    if (is_sleeping == true)
    {
        // 发布唤醒事件到 /voice_wakeup 话题
        ROSManager::getInstance().publishVoiceWakeup(wake_str);
        is_sleeping = false;
    }

    // 发布唤醒词到 /chat_history 话题
    nlohmann::json chat_wake = {
        {"speaker", "PERSON"},
        {"content", "灵犀灵犀"}
    };
    ROSManager::getInstance().publishChatConversation(chat_wake.dump());

    // 将唤醒词“灵犀灵犀”发送给LLM，闲聊式对话
    ChatAndSpeak("灵犀灵犀");

    std::cout << "接收到唤醒事件: " << wake_str << std::endl;
    return;
}

void AvvtnCapture::handleTouchWake()
{
    LOG_INFO("收到触摸唤醒事件 /touch_wakeup");

    is_sleeping = false;

    // system("ffplay -autoexit -nodisp -ar 24000 -ac 1 -f f32le /home/jetson/local_cmcc_robot/bin/output.pcm > /dev/null 2>&1 &");

    LOG_INFO("触摸唤醒已执行");
}

