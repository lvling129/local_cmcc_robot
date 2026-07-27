/*
 * @Author: zwli24
 * @email: zwli24@iflytek.com
 * @Date: 2025-09-25 11:23:27
 * @LastEditTime: 2025-10-16 21:51:29
 * @Description: 多模态测试demo，包含数据采集 送入引擎 aiui解析（aiui建议参考官网demo）
 */
#ifndef AVVTN_CAPTURE_H
#define AVVTN_CAPTURE_H

#include <iostream>
#include <string>
#include <atomic>
#include <cstdint>

#include "audio_capture/audio_capture.h"
#include "asr/sherpa_asr.h"
#include "tts/sherpa_tts.h"
#include "tts/audio_player.h"
#include "llm/llm_client.h"
#include "llm/remote_llm_client.h"

// 0 = 本地 qwen3-8b, 1 = 远程 qwen
#define USE_REMOTE_LLM 0
#include "avvtn_api/avvtn_api.h"
#include "utils/cjson/cJSON.h"
// 错误检查宏，如果返回值不为0则直接返回该值
#define CHECK_RET(ret) \
    if (ret != 0) return ret;

/**
 * @brief 多模态数据采集类
 *
 * 该类负责协调视频采集、音频采集和多模态降噪引擎的工作
 * 主要功能包括：
 * 1. 初始化视频和音频采集设备
 * 2. 初始化多模态降噪引擎
 * 3. 处理视频和音频数据的回调
 * 4. 将采集到的数据送入降噪引擎进行处理
 */
class AvvtnCapture
{
public:
    // 默认构造函数
    AvvtnCapture() = default;
    // 默认析构函数
    ~AvvtnCapture() = default;

    /**
     * @brief 初始化多模态采集系统
     * @param avvtn_cfg_path 多模态降噪引擎配置文件路径
     * @return 0表示成功，非0表示失败
     */
    int Init(std::string avvtn_cfg_path);

    /**
     * @brief 销毁多模态采集系统，释放资源
     * @return 0表示成功，非0表示失败
     */
    int Destory();

    // 在类中添加静态获取函数
    static AvvtnCapture* getInstance() {
        return g_avvtn_capture_instance;
    }

    /**
     * @brief 触摸唤醒处理（由 /touch_wakeup 话题回调触发）
     */
    void handleTouchWake();

    /**
     * @brief 设置休眠状态
     * @param sleeping true=进入休眠，false=唤醒
     */
    void setSleeping(bool sleeping) { is_sleeping = sleeping; }

    /**
     * @brief TTS 语音合成并播放
     * @param text 要合成的文本
     * @param speed 语速，默认 1.0
     * @param append_mode 追加模式，true=不打断当前播放（流式追加用）
     */
    void Speak(const std::string& text, float speed = 1.0f, bool append_mode = false, uint64_t session_id = 0);

    /**
     * @brief 发送文本给 LLM 闲聊模型，流式回复触发 TTS 播放
     * @param text 要发送给 LLM 的文本
     */
    void ChatAndSpeak(const std::string& text);

    /**
     * @brief 测试设置beam
     * @param beam_id beamID
     * @return 0表示成功，非0表示失败
     */
    int test_set_beam(const char *beam_id);

private:
    static AvvtnCapture* g_avvtn_capture_instance;
private:
    /**
     * @brief 音频采集回调函数（静态函数）
     * @param userdata 用户数据指针，指向AvvtnCapture实例
     * @param audio 音频数据指针
     * @param len 音频数据长度（字节数）
     */
    static void audioCaptureCallback(void *userdata, const void *audio, int len);

    /**
     * @brief 多模态降噪引擎回调函数（静态函数）
     * @param data_p 回调数据指针
     * @param user_data 用户数据指针，指向AvvtnCapture实例
     * @return 0表示成功，非0表示失败
     */
    static int avvtnCallback(avvtn_callback_data_t *data_p, void *user_data);

    /**
     * @brief 解析json数据并检查数据
     * @param data_p 回调数据指针
     * @param json 解析后的json数据
     * @param data 解析后的数据
     * @return 0表示成功，非0表示失败
     */
    int parseJsonAndCheckData(avvtn_callback_data_t *data_p, cJSON **json, cJSON **data);

    /**
     * @brief 处理音频CAE回调
     * @param data_p 回调数据指针
     */
    void handleAudioCAE(avvtn_callback_data_t *data_p);

    /**
     * @brief 处理音频录音回调
     * @param data_p 回调数据指针
     */
    void handleAudioRec(avvtn_callback_data_t *data_p);

    /**
     * @brief 处理音频唤醒回调
     * @param data_p 回调数据指针
     */
    void handleAudioWake(avvtn_callback_data_t *data_p);

    /**
     * @brief 测试评估关键词
     * @param keyword 关键词
     * @return 0表示成功，非0表示失败
     */
    int test_evaluate_keyword(const char *keyword);

    /**
     * @brief 测试生成关键词
     * @param keyword 关键词
     * @param output_path 输出路径
     * @return 0表示成功，非0表示失败
     */
    int test_generate_keyword(const char *keyword, const char *output_path);

    /**
     * @brief 测试添加关键词
     * @param output_path 输出路径
     * @return 0表示成功，非0表示失败
     */
    int test_add_keyword(const char *output_path);

    /**
     * @brief 测试删除关键词
     * @param keywords_id 关键词ID
     * @return 0表示成功，非0表示失败
     */
    int test_remove_keyword(const char *keywords_id);

    /**
     * @brief 测试设置参数
     * @param param 参数
     * @return 0表示成功，非0表示失败
     */
    int test_set_param(const char *param);

    /**
     * @brief 测试多模态降噪引擎
     * @return 0表示成功，非0表示失败
     */
    int test_avvtn();

private:
    // 音频采集模块句柄，负责从麦克风读取音频数据
    AudioCapture audio_cap_;

    // 多模态降噪引擎初始化参数结构体
    avvtn_init_param_t init_param_;

    // 多模态降噪引擎句柄，用于处理音视频数据
    avvtn_handle avvtn_cap_ = nullptr;

    // sherpa-onnx 本地离线 ASR (SenseVoice)
    SherpaAsr sherpa_asr_;

    // sherpa-onnx 本地 TTS (Matcha)
    SherpaTts sherpa_tts_;

    // 音频播放器（支持打断）
    AudioPlayer audio_player_;

    // 本地大模型客户端
    LlmClient llm_intent_;   // 意图识别模型 (qwen3-1.7b, port 8081)
    LlmClient llm_chat_;     // 对话模型 (qwen3-8b, port 8080)

    // 远程闲聊模型客户端
    RemoteLlmClient llm_remote_;  // 远程 qwen (10.33.225.63:9090)

private:
    std::string wake_mode_ = "ivw";           // 唤醒模式
    bool is_sleeping = true;           //是否已经休眠，等待唤醒

    // LLM 会话计数器：每次 ChatAndSpeak 自增，过期会话的 LLM 回调直接丢弃
    std::atomic<uint64_t> chat_session_id_{0};
};

#endif