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

#include "aiui_capture/aiui_wapper.h"
#include "audio_capture/audio_capture.h"
#include "asr/sherpa_asr.h"
#include "tts/sherpa_tts.h"
#include "tts/audio_player.h"
#include "llm/llm_client.h"
#include "avvtn_api/avvtn_api.h"
#include "utils/cjson/cJSON.h"
#include "video_capture/video_capture.h"
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
     * @param aiui_cfg_path AIUI配置文件路径
     * @return 0表示成功，非0表示失败
     */
    int Init(std::string avvtn_cfg_path, std::string aiui_cfg_path);

    /**
     * @brief 销毁多模态采集系统，释放资源
     * @return 0表示成功，非0表示失败
     */
    int Destory();

    static void onStarted();
    static void onPaused();
    static void onResumed();
    static void onStopped();
    static void onError(int error, const char *des);
    static void onProgress(int streamId, int progress, const char *audio, int len, bool isCompleted);

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
    void Speak(const std::string& text, float speed = 1.0f, bool append_mode = false);

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

    // 定时器私有静态成员（仅类内部访问，线程安全）
    static std::atomic<bool> g_timer_running;
    static std::atomic<long long> g_last_active_time;
    static std::thread g_timer_thread;

    // 定时器核心循环（私有静态函数）
    static void timerLoop();
    // 获取当前毫秒时间戳（私有内联函数）
    static inline long long getCurrentTimeMs();

    static void onPlayerTimeout();  // 定时器超时目标函数
    static void stopPlayerTimer();  // 统一停止定时器函数

private:
    /**
     * @brief 视频采集回调函数（静态函数）
     * @param userdata 用户数据指针，指向AvvtnCapture实例
     * @param image 视频图像数据指针
     * @param width 图像宽度
     * @param height 图像高度
     */
    static void videoCaptureCallback(void *userdata, const void *image, int width, int height);

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
     * @brief AIUI回调函数（静态函数）
     * @param user_data 用户数据指针，指向AvvtnCapture实例
     * @param event AIUI事件
     */
    static void aiuiCallback(void *user_data, const IAIUIEvent &event);
    /**
     * @brief 解析json数据并检查数据
     * @param data_p 回调数据指针
     * @param json 解析后的json数据
     * @param data 解析后的数据
     * @return 0表示成功，非0表示失败
     */
    int parseJsonAndCheckData(avvtn_callback_data_t *data_p, cJSON **json, cJSON **data);

    /**
     * @brief 处理人脸识别回调
     * @param data_p 回调数据指针
     */
    void handleFaceRecognition(avvtn_callback_data_t *data_p);

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
     * @brief 处理AIUI识别回调
     * @param buffer 识别结果
     */
    void handleAiuiIat(Json::Reader &reader, const char *buffer, int len);

    /**
     * @brief 处理AIUI合成回调
     * @param buffer 合成结果
     */
    void handleAiuiTts(const Json::Reader &reader, const Json::Value content, const IAIUIEvent event, Json::Value bizParamJson, const char *buffer, int len);

    /**
     * @brief 处理AIUI流式nlp回调
     * @param buffer 流式nlp结果
     */
    void handleAiuiStreamNlp(Json::Reader &reader, const char *buffer, int len);

    /**
     * @brief 处理 AIUI 返回的语义规整结果 (cbm_tidy)
     * @param resultStr JSON 字符串格式的语义规整结果
     */
    void handleCbmTidy(const std::string& resultStr);

    /**
     * @brief 处理 AIUI 返回的传统语义技能结果 (cbm_semantic)
     * @param resultStr JSON 字符串格式的传统语义技能结果
     * @return bool 是否命中技能 (true: 命中技能, false: 未命中技能或解析失败)
     */
    bool handleCbmSemantic(const std::string& resultStr);

    /**
     * @brief 处理 AIUI 返回的意图落域结果 (cbm_tool_pk)，无返回值版本
     * @param resultStr JSON 字符串格式的意图落域结果
     */
    void handleCbmToolPk(const std::string& resultStr);

    /**
     * @brief 处理 AIUI 返回的知识分类结果 (cbm_retrieval_classify)
     * @param resultStr JSON 字符串格式的知识分类结果
     */
    void handleCbmRetrievalClassify(const std::string& resultStr);

    /**
     * @brief 处理 AIUI 返回的知识溯源结果 (cbm_knowledge)
     * @param resultStr JSON 字符串格式的知识溯源结果
     */
    void handleCbmKnowledge(const std::string& resultStr);

    /**
     * @brief 处理 命中的技能
     * @param text_str JSON 字符串格式的技能text
     */
    void handleSkill(const std::string& text_str);

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
    // 视频采集模块句柄，负责从摄像头读取视频数据
    VideoCapture video_cap_;

    // 音频采集模块句柄，负责从麦克风读取音频数据
    AudioCapture audio_cap_;

    // 多模态降噪引擎初始化参数结构体
    avvtn_init_param_t init_param_;

    // 多模态降噪引擎句柄，用于处理音视频数据
    avvtn_handle avvtn_cap_ = nullptr;

    // AIUI句柄，用于处理语音识别和合成
    AiuiWrapper aiui_wrapper_;

    // sherpa-onnx 本地离线 ASR (SenseVoice)
    SherpaAsr sherpa_asr_;

    // sherpa-onnx 本地 TTS (Matcha)
    SherpaTts sherpa_tts_;

    // 音频播放器（支持打断）
    AudioPlayer audio_player_;

    // 本地大模型客户端
    LlmClient llm_intent_;   // 意图识别模型 (qwen3-1.7b, port 8081)
    LlmClient llm_chat_;     // 对话模型 (qwen3-8b, port 8080)

private:
    int scaling_factor_ = 2;    // 缩放因子
    cv::Mat receive_image_;     // 接收到的原始图像
    cv::Mat resized_image_;     // 缩放后的图像

    std::string wake_mode_ = "ivw";           // 唤醒模式
    std::string current_tts_sid_;             // 当前合成sid
    std::string current_iat_sid_;             // 当前识别sid
    std::string iat_text_buffer_;             // 识别结果缓存
    std::string stream_nlp_answer_buffer_;    // 流式nlp的应答语缓存
    int tts_len_          = 0;                // 当前收到了tts音频的长度
    int intent_cnt_       = 0;                // 意图的数量
    int stream_nlp_index_ = 0;                // 流式nlp的索引

    std::string ignore_tts_sid_;              // 当前tts不播放，播放技能返回tts

    bool is_skill = false;      //是否命中技能
    bool is_knowledge = false;  //是否命中知识库
    bool is_playing;            //播放器是否正在播放
    bool is_sleeping = true;           //是否已经休眠，等待唤醒
};

#endif