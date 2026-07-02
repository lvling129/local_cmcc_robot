#include "avvtn_capture/avvtn_capture.h"
#include "utils/Logger.hpp"
#include "ros2/ros_manager.hpp"

#include <fstream>
#include <sstream>

// 读取 prompt 文件
static std::string ReadPromptFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("无法打开 prompt 文件: %s", path.c_str());
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

AvvtnCapture* AvvtnCapture::g_avvtn_capture_instance = nullptr;

// 初始化类静态成员（必须在类外初始化）
std::atomic<bool> AvvtnCapture::g_timer_running(false);
std::atomic<long long> AvvtnCapture::g_last_active_time(0LL); // 0LL表示long long类型的0
std::thread AvvtnCapture::g_timer_thread;

// 实现时间戳函数
inline long long AvvtnCapture::getCurrentTimeMs() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// 实现超时目标函数
void AvvtnCapture::onPlayerTimeout() {
    LOG_DEBUG("播放器1秒无进度更新，触发超时回调！");

    AvvtnCapture* instance = g_avvtn_capture_instance;
    if (instance == nullptr) {
        LOG_WARN("onPlayerTimeout: 实例已销毁，跳过处理");
        stopPlayerTimer();
        return;
    }

    if (instance->is_sleeping == true)
    {
        ROSManager::getInstance().publishStatus("STATUS_WAITING_WAKEUP");
    }
    else
    {
        ROSManager::getInstance().publishStatus("STATUS_WAITING_CONVERSATION");
    }

    instance->is_playing = false;

    stopPlayerTimer();
}

// 实现定时器循环
void AvvtnCapture::timerLoop() {
    const int TIMEOUT_MS = 1000;
    while (g_timer_running) {
        long long current = getCurrentTimeMs();
        long long last = g_last_active_time.load();
        if (last != 0 && (current - last) >= TIMEOUT_MS) {
            onPlayerTimeout();
            g_last_active_time = 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

/*********************播放回调函数************************/
void AvvtnCapture::onStarted()
{
    std::cout << "PcmPlayer, onStarted" << std::endl;
}

void AvvtnCapture::onPaused()
{
    std::cout << "PcmPlayer, onPaused" << std::endl;
}

void AvvtnCapture::onResumed()
{
    std::cout << "PcmPlayer, onResumed" << std::endl;
}

void AvvtnCapture::onStopped()
{
    std::cout << "PcmPlayer, onStopped" << std::endl;
}

void AvvtnCapture::onError(int error, const char *des)
{
    std::cout << "PcmPlayer, onError, error=" << error << ", des=" << des << std::endl;
}

void AvvtnCapture::onProgress(int streamId, int progress, const char *audio, int len, bool isCompleted)
{
    //std::cout << "PcmPlayer, onProgress, streamId=" << streamId << ", progress=" << progress << ", len=" << len << ", isCompleted=" << isCompleted << std::endl;
    AvvtnCapture* instance = g_avvtn_capture_instance;
    if (instance == nullptr) {
        LOG_WARN("onProgress: 实例已销毁，跳过处理");
        return;
    }

    instance->is_playing = true;

    // 重置定时器：更新最后活动时间戳
    g_last_active_time = getCurrentTimeMs();
    // 首次触发时启动定时器线程
    if (!g_timer_running) {
        g_timer_running = true;
        g_timer_thread = std::thread(timerLoop);
        g_timer_thread.detach();
    }

    // 原有业务逻辑
    if (isCompleted) {
        LOG_DEBUG("音频播放完成！streamId=%d\n", streamId);
    }

    if (progress == 100) {
        LOG_DEBUG("播放进度已达到100%%\n");
    }
}

// 实现停止定时器函数
void AvvtnCapture::stopPlayerTimer() {
    g_timer_running = false;
    g_last_active_time = 0;
}

// 初始化
int AvvtnCapture::Init(std::string avvtn_cfg_path, std::string aiui_cfg_path)
{
    int ret = 0;
    LOG_INFO("初始化多模态降噪引擎AVVTN");
    g_avvtn_capture_instance = this;
    // 1、初始化多模态降噪引擎
    std::string avvtn_input_str    = "{ \"params\":{ \"cfg_path\":\"" + avvtn_cfg_path + "\" } }";
    init_param_.callback.handler   = avvtnCallback;
    init_param_.callback.user_data = this;
    init_param_.input              = avvtn_input_str.c_str();
    ret                            = (int)avvtn_api_init(&avvtn_cap_, &init_param_);
    // ret非0代表失败了
    CHECK_RET(ret);
    LOG_INFO("读取AVVTN配置文件: %s", avvtn_cfg_path.c_str());
    if (0 != ret)
    {
        LOG_ERROR("初始化多模态降噪引擎AVVTN失败");
    }
    else
    {
        LOG_INFO("初始化多模态降噪引擎AVVTN成功");
    }

    LOG_INFO("初始化AIUI");
    // 2、初始化 aiui(可选，如果接入自己的大模型和识别引擎则将这块剥离)
    aiui_init_param_t aiui_init_param;
    aiui_init_param.param.cfg_path     = aiui_cfg_path;
    aiui_init_param.callback.handler   = aiuiCallback;
    aiui_init_param.callback.user_data = this;
    ret                                = aiui_wrapper_.Init(aiui_init_param);
    CHECK_RET(ret);
    LOG_DEBUG("读取AIUI配置文件: %s", aiui_cfg_path.c_str());
    if (0 != ret)
    {
        LOG_ERROR("初始化AIUI失败");
    }
    else
    {
        LOG_INFO("初始化AIUI成功");
    }

    // 初始化pcm播放器回调
    aiui_pcm_player_set_callbacks(onStarted, onPaused, onResumed, onStopped, onProgress, onError);

    // 3、初始化 SenseVoice 本地 ASR
    LOG_INFO("初始化 SenseVoice 本地 ASR");
    std::string project_dir = avvtn_cfg_path.substr(0, avvtn_cfg_path.find_last_of('/'));
    std::string asr_model_dir = project_dir + "/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17";
    ret = sherpa_asr_.Init(asr_model_dir);
    if (ret != 0)
    {
        LOG_ERROR("初始化 sherpa-onnx ASR 失败");
    }
    else
    {
        LOG_INFO("初始化 sherpa-onnx ASR 成功");
    }

    // 4、初始化 Matcha TTS
    LOG_INFO("初始化 Matcha TTS");
    std::string tts_model_dir = project_dir + "/matcha-icefall-zh-baker";
    std::string vocoder_path = tts_model_dir + "/hifigan_v2.onnx";
    ret = sherpa_tts_.Init(tts_model_dir, vocoder_path);
    if (ret != 0)
    {
        LOG_ERROR("初始化 Matcha TTS 失败");
    }
    else
    {
        LOG_INFO("初始化 Matcha TTS 成功");
    }

    // 5、初始化音频播放器
    LOG_INFO("初始化音频播放器");
    ret = audio_player_.Init("default", sherpa_tts_.GetSampleRate());
    if (ret != 0)
    {
        LOG_ERROR("初始化音频播放器失败");
    }
    else
    {
        LOG_INFO("初始化音频播放器成功");
    }

    // 5.1、初始化 LLM 客户端
    LOG_INFO("初始化 LLM 客户端");

    const std::string prompt_dir = "/home/nvidia/local_cmcc_robot/resource/prompts";

    // 意图识别模型 (qwen3-1.7b)
    ret = llm_intent_.Init("http://127.0.0.1:8081");
    if (ret != 0)
    {
        LOG_ERROR("初始化意图识别 LLM 失败");
    }
    else
    {
        std::string intent_prompt = ReadPromptFile(prompt_dir + "/intent.prompt");
        if (!intent_prompt.empty()) {
            llm_intent_.SetSystemPrompt(intent_prompt);
            LOG_INFO("加载意图识别 prompt: %s/intent.prompt", prompt_dir.c_str());
        }
        llm_intent_.SetParams(0.1f, 64);  // 低温度、短回复
        LOG_INFO("初始化意图识别 LLM 成功 (port 8081)");
    }

    // 对话模型 (qwen3-8b)
    ret = llm_chat_.Init("http://127.0.0.1:8080");
    if (ret != 0)
    {
        LOG_ERROR("初始化对话 LLM 失败");
    }
    else
    {
        std::string chat_prompt = ReadPromptFile(prompt_dir + "/chat.prompt");
        if (!chat_prompt.empty()) {
            llm_chat_.SetSystemPrompt(chat_prompt);
            LOG_INFO("加载对话 prompt: %s/chat.prompt", prompt_dir.c_str());
        }
        llm_chat_.SetParams(0.7f, 512);
        LOG_INFO("初始化对话 LLM 成功 (port 8080)");
    }

    // 6、初始化视频采集
    // ret = video_cap_.Start(this, videoCaptureCallback);
    // CHECK_RET(ret);

    LOG_INFO("初始化音频采集");
    // 7、初始化音频采集
    ret = audio_cap_.Start(this, audioCaptureCallback);
    CHECK_RET(ret);
    if (0 != ret)
    {
        LOG_ERROR("初始化音频采集失败");
    }
    else
    {
        LOG_INFO("初始化音频采集成功");
        LOG_INFO("开始音频采集...");
        ROSManager::getInstance().publishStatus("STATUS_WAITING_WAKEUP");
    }

    // test_avvtn();
    return 0;
}

// 销毁
int AvvtnCapture::Destory()
{
    int ret = 0;
    // 1、停止视频采集
    // ret = video_cap_.Stop();
    // CHECK_RET(ret);

    // 先停止定时器，确保回调不再访问成员变量
    stopPlayerTimer();

    // 再清空全局实例指针，后续回调会因空指针检查而跳过
    g_avvtn_capture_instance = nullptr;

    // 2、停止音频采集
    ret = audio_cap_.Stop();
    CHECK_RET(ret);

    // 3、销毁多模态降噪引擎
    avvtn_api_destroy(avvtn_cap_);

    // 4、销毁 sherpa-onnx ASR
    sherpa_asr_.Destroy();

    // 5、销毁音频播放器
    audio_player_.Destroy();

    // 6、销毁 sherpa-onnx TTS
    sherpa_tts_.Destroy();

    // 7、销毁AIUI
    aiui_wrapper_.Destory();
    return 0;
}

// TTS 语音合成并播放
void AvvtnCapture::Speak(const std::string& text, float speed)
{
    if (!sherpa_tts_.IsInitialized()) {
        LOG_WARN("TTS 未初始化，无法合成语音");
        return;
    }

    if (text.empty()) {
        LOG_WARN("TTS 输入文本为空");
        return;
    }

    // 检查是否包含中文字符（UTF-8 中文范围 0xE4-0xE9 开头）
    bool has_chinese = false;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= 0xE4 && c <= 0xE9) {
            has_chinese = true;
            break;
        }
    }

    if (!has_chinese) {
        LOG_WARN("TTS 跳过非中文文本: \"%s\"", text.c_str());
        return;
    }

    LOG_INFO("TTS 播放: \"%s\"", text.c_str());

    // 打断当前播放
    audio_player_.Interrupt();

    // 流式合成：边合成边播放
    sherpa_tts_.Generate(text, [this](const AudioChunk& chunk, float progress) -> bool {
        // 将音频块送入播放器
        audio_player_.StreamPush(chunk.samples.data(), chunk.samples.size());
        return true;  // 继续合成
    }, speed);

    // 通知播放结束
    audio_player_.StreamEnd();
}

// 测试多模态降噪引擎
int AvvtnCapture::test_avvtn()
{
    int ret = 0;
    // 测试设置波束
    ret = test_set_beam("{\"params\":{\"beam\":\"1\"}}");
    if (ret != 0)
    {
        std::cerr << "Failed to set beam" << std::endl;
        return ret;
    }
    // 测试修改参数
    ret = test_set_param("{\"params\":{\"cae_mode\":\"ivw\"}}");
    if (ret != 0)
    {
        std::cerr << "Failed to set param" << std::endl;
        return ret;
    }

    ret = test_set_param("{\"params\":{\"log_save\":\"1\"}}");
    if (ret != 0)
    {
        std::cerr << "Failed to set param" << std::endl;
        return ret;
    }

    // 测试生成关键词 具体步骤 1、评估唤醒词（可选） 2、生成唤醒词 3、添加唤醒词 4、移除唤醒词
    // 为了测试我这边都没判断ret返回值，实际使用中请根据ret返回值来判断是否成功 0表示成功 非0表示失败

    // 1、评估唤醒词（测试语料 一一一一）
    // 注意 输入的格式必须是GBK，如果是UTF-8则返回的都是A等级
    // 可选，也可以不做评估直接生成唤醒词，建议只有A等级的词才生成唤醒词
    // 返回的json格式为：{ "params":{ "level":"A" } }
    // 其中level为A表示这个关键词的优秀等级 A为优秀 B为良好 C为一般
    // 用户可以根据这个等级来决定是否使用这个关键词 推荐A才保存唤醒词资源
    const char gbk_word_bytes[] = { 0xd2, 0xbb, 0xd2, 0xbb, 0xd2, 0xbb, 0xd2, 0xbb, 0x00 };
    ret                         = test_evaluate_keyword(gbk_word_bytes);
    if (ret != 0)
    {
        std::cerr << "Failed to evaluate keyword" << std::endl;
        return ret;
    }
    // 2、生成唤醒词
    // 经过第一步的评估后，如果等级为A，则可以生成唤醒词
    // 返回的二进制文件为唤醒词资源，可以用于添加唤醒词，自行选择是否保存以及保存地址
    // 这一步不需要转gbk 直接utf-8即可，如果要生成多个唤醒词资源，请在每个词中间添加英文逗号
    ret = test_generate_keyword("{ \"params\":{ \"word\":\"小爱同学\" } }", "./xatx.bin");
    if (ret != 0)
    {
        std::cerr << "Failed to generate keyword" << std::endl;
        return ret;
    }
    ret = test_generate_keyword("{ \"params\":{ \"word\":\"小明小明,小美小美,小红小红\" } }", "./xmxm.bin");
    if (ret != 0)
    {
        std::cerr << "Failed to generate keyword" << std::endl;
        return ret;
    }

    // 3、添加唤醒词
    // 返回的json文件内容如下：{ "params":{ "result":"0", "id":"500" } }
    // 其中result为0表示成功，id为关键词的ID(如果后续删除时，需要用到 比如小塔小塔的id是500)
    // 注意 一个资源文件一个id,比如小明小明，小美小美，小红小红就一个id。
    ret = test_add_keyword("./xatx.bin");
    if (ret != 0)
    {
        std::cerr << "Failed to add keyword" << std::endl;
        return ret;
    }
    ret = test_add_keyword("./xmxm.bin");
    if (ret != 0)
    {
        std::cerr << "Failed to add keyword" << std::endl;
        return ret;
    }

    // 4、移除唤醒词
    // 返回的json文件内容如下：{ "params":{ "result":"0" } }
    // 其中result为0表示成功
    ret = test_remove_keyword("{ \"params\":{ \"id\":\"600\" } }");
    if (ret != 0)
    {
        std::cerr << "Failed to remove keyword" << std::endl;
        return ret;
    }

    return 0;
}

// 视频回调
void AvvtnCapture::videoCaptureCallback(void *userdata, const void *image, int width, int height)
{
    AvvtnCapture *self                        = (AvvtnCapture *)userdata;
    avvtn_interact_info_t avvtn_interact_info = {};
    avvtn_interact_info.type                  = AVVTN_INTERACT_TYPE_FEED_VIDEO;
    avvtn_interact_info.in.raw                = const_cast<void *>(image);
    avvtn_interact_info.in.raw_size           = width * height * 3;
    avvtn_api_interact(self->avvtn_cap_, &avvtn_interact_info);
    return;
}

// 音频回调
void AvvtnCapture::audioCaptureCallback(void *userdata, const void *audio, int len)
{
    AvvtnCapture *self                        = (AvvtnCapture *)userdata;
    avvtn_interact_info_t avvtn_interact_info = {};
    avvtn_interact_info.type                  = AVVTN_INTERACT_TYPE_FEED_AUDIO;
    avvtn_interact_info.in.raw                = const_cast<void *>(audio);
    avvtn_interact_info.in.raw_size           = len;
    avvtn_api_interact(self->avvtn_cap_, &avvtn_interact_info);
    return;
}

// 多模态降噪引擎回调
int AvvtnCapture::avvtnCallback(avvtn_callback_data_t *data_p, void *user_data)
{
    AvvtnCapture *self = static_cast<AvvtnCapture *>(user_data);
    // 根据回调类型进行不同的处理
    switch (data_p->type)
    {
        // 降噪音频回调，这个数据是一直对外抛出的，无论有没有声音。
        case AVVTN_CALLBACK_TYPE_AUDIO_CAE:
        {
            self->handleAudioCAE(data_p);
        }
        break;
        // 识别音频回调，这个数据是只有有声音时才对外抛出的。
        case AVVTN_CALLBACK_TYPE_AUDIO_REC:
        {
            self->handleAudioRec(data_p);
        }
        break;
        // 人脸识别回调，多模态模式下一直对外抛出
        case AVVTN_CALLBACK_TYPE_FACE_REC:
        {
            // self->handleFaceRecognition(data_p);
        }
        break;
        // 唤醒事件回调，注意，一次唤醒会抛出两次，一次带角度，一次不带角度。
        case AVVTN_CALLBACK_TYPE_AUDIO_WAKE:
        {
            self->handleAudioWake(data_p);
        }
        break;
        default:
            break;
    }
    return 0;
}