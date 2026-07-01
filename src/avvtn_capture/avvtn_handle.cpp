#include "avvtn_capture/avvtn_capture.h"
#include "utils/Logger.hpp"
#include "utils/json.hpp"
#include "ros2/ros_manager.hpp"
#include <cstdlib>
#include <chrono>

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

    // 打印音频回调间隔和数据量（用于确认发送周期）
    // 调试用，后续删除，包含#include <chrono>
/*
    {
        static auto last_time = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        auto interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        last_time = now;
        // 16kHz mono S16LE: 1ms = 32字节
        int audio_duration_ms = data_p->data_size / 32;
        LOG_INFO("REC音频回调: data_size=%d, 音频时长=%dms, channel=%d, vad_status=%d, 回调间隔=%lldms",
                 data_p->data_size, audio_duration_ms, channel, vad_status, interval_ms);
    }
*/

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

                // 发布到 ROS2 话题
                nlohmann::json ask = {
                    {"speaker", "person"},
                    {"text", final_text}
                };
                ROSManager::getInstance().publishChatHistory(ask.dump());
                ROSManager::getInstance().publishChatHistoryNoStream(ask.dump());

                // TODO: 将 ASR 结果发送给大模型，获取回复后调用 Speak()
                // 目前回显 ASR 结果用于测试 TTS
                Speak(final_text);
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

void AvvtnCapture::handleFaceRecognition(avvtn_callback_data_t *data_p)
{
    static int image_w = 0;
    static int image_h = 0;

    if (!data_p || !data_p->param || !data_p->data || data_p->data_size <= 0)
    {
        std::cerr << "Invalid callback data!" << std::endl;
        return;
    }

    // data_p->param 是json格式，需要解析json数据，包含了各个通道的人脸信息。
    cJSON *root = cJSON_Parse((char *)data_p->param);
    if (root == nullptr)
    {
        std::cerr << "Failed to parse JSON!" << std::endl;
        return;
    }
    cJSON *format = cJSON_GetObjectItem(root, "format");
    if (format == nullptr)
    {
        std::cerr << "No format object found!" << std::endl;
        cJSON_Delete(root);
        return;
    }
    if (cJSON_IsNumber(cJSON_GetObjectItem(format, "image_w")) && cJSON_IsNumber(cJSON_GetObjectItem(format, "image_h")))
    {
        image_w = cJSON_GetObjectItem(format, "image_w")->valueint;
        image_h = cJSON_GetObjectItem(format, "image_h")->valueint;
    }
    else
    {
        std::cerr << "Invalid image width or height!" << std::endl;
        cJSON_Delete(root);
        return;
    }
    // data_p->data 是视频数据，data_p->data_size 是视频数据大小
    receive_image_ = cv::Mat(image_h, image_w, CV_8UC3, (void *)data_p->data);
    // 缩放只是为了方便后续的视频显示，实际使用当中可以不做缩放
    // resize操作在算力较弱或者占用较高的主板中可能会造成耗时过长，导致回调函数卡住，建议在实际生产环境中不要使用resize。
    cv::resize(receive_image_, resized_image_, cv::Size(image_w / scaling_factor_, image_h / scaling_factor_));

    cJSON *list = cJSON_GetObjectItem(root, "list");
    if (list == nullptr || cJSON_GetArraySize(list) == 0)
    {
        std::cerr << "No face data found!" << std::endl;
        cJSON_Delete(root);
        return;
    }
    // 遍历每个人脸，绘制人脸框
    int num_faces = cJSON_GetArraySize(list);
    for (int i = 0; i < num_faces; ++i)
    {
        cJSON *face  = cJSON_GetArrayItem(list, i);
        bool hasFace = cJSON_GetObjectItem(face, "hasFace")->valueint != 0;
        if (!hasFace)
        {
            continue;
        }
        // 获取每个人脸的坐标和尺寸
        int x         = cJSON_GetObjectItem(face, "x")->valueint;
        int y         = cJSON_GetObjectItem(face, "y")->valueint;
        int w         = cJSON_GetObjectItem(face, "w")->valueint;
        int h         = cJSON_GetObjectItem(face, "h")->valueint;
        bool mouthOcc = cJSON_GetObjectItem(face, "mouthOcc")->valueint != 0;
        // 绘制人脸框，缩放坐标值
        cv::Scalar color = mouthOcc ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0);
        cv::rectangle(resized_image_, cv::Point(x / scaling_factor_, y / scaling_factor_), cv::Point((x + w) / scaling_factor_, (y + h) / scaling_factor_), color, 2);

        // 在人脸框上方显示索引编号
        std::string label  = std::to_string(i);
        int baseline       = 0;
        cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.6, 2, &baseline);
        cv::Point text_pos(x / scaling_factor_, y / scaling_factor_ - 5);
        cv::putText(resized_image_, label, text_pos, cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
    }
    cJSON_Delete(root);

// 是否显示图像，建议在实际生产环境中不要在回调函数中显示图像，因为显示图像会占用大量算力，导致回调函数卡住。
#if 1
    cv::imshow("Resized Image", resized_image_);
    cv::waitKey(1);
#endif
    return;
}

void AvvtnCapture::handleAudioWake(avvtn_callback_data_t *data_p)
{
    // 唤醒会触发两次handleAudioWake，先wakeup后wakeup_detail
    std::string wake_str = std::string((char *)data_p->data, data_p->data_size);
    LOG_INFO("AVVTN接收到唤醒语音: %s", wake_str.c_str());

    if (is_sleeping == false)
    {
        LOG_INFO("AVVTN已处于唤醒状态，不执行唤醒");
        return;
    }

    //aiui_wrapper_.ResetWakeup();
    is_sleeping = false;

    // 发布唤醒事件到 /voice_wakeup 话题
    ROSManager::getInstance().publishVoiceWakeup(wake_str);
    
    std::cout << "接收到唤醒事件: " << wake_str << std::endl;
    return;
}

void AvvtnCapture::handleTouchWake()
{
    LOG_INFO("收到触摸唤醒事件 /touch_wakeup");

    is_sleeping = false;

    // system("ffplay -autoexit -nodisp -ar 24000 -ac 1 -f f32le /home/nvidia/local_cmcc_robot/bin/output.pcm > /dev/null 2>&1 &");

    //aiui_wrapper_.Wakeup();
    LOG_INFO("触摸唤醒已执行");
}

