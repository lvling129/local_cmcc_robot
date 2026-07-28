#include "avvtn_capture/avvtn_capture.h"
#include "utils/Logger.hpp"
#include "utils/json.hpp"
#include "ros2/ros_manager.hpp"

#include <fstream>
#include <sstream>
#include <cstring>

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

// 清理 TTS 文本：去掉 markdown 符号、引号和 emoji，避免读出符号或产生怪停顿
static std::string CleanForTts(std::string text) {
    static const char* drops[] = {"*", "#", "`", "\"", "“", "”", "‘", "’",
                                  "（", "）", "(", ")", "[", "]", "{", "}"};
    for (const char* d : drops) {
        size_t pos;
        while ((pos = text.find(d)) != std::string::npos) {
            text.erase(pos, strlen(d));
        }
    }
    for (auto& c : text) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }
    // 去掉 4 字节 UTF-8 序列（emoji 等补充平面字符，TTS 无法处理）
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= 0xF0) {
            i += 4;  // 跳过 4 字节序列（emoji 等）
        } else if (c >= 0xE0) {
            result.append(text, i, 3);
            i += 3;
        } else if (c >= 0xC0) {
            result.append(text, i, 2);
            i += 2;
        } else {
            result += text[i];
            i += 1;
        }
    }
    // 去掉前后空白
    size_t start = result.find_first_not_of(" \t\r\n");
    size_t end = result.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return result.substr(start, end - start + 1);
}

// 从 buffer 中提取一个完整句子（精确切到标点位置，残余保留在 buffer）
// is_first: 首句用更短的逗号阈值，尽早出声降低响应延迟
static std::string ExtractCompleteSentence(std::string& buffer, bool is_first) {
    static const char* strong_marks[] = {"。", "！", "？", "；", "\n"};

    // 找最后一个强边界标点（一次切出多个完整句，合成更连贯）
    size_t split = std::string::npos;
    for (const char* mark : strong_marks) {
        size_t pos = buffer.rfind(mark);
        if (pos != std::string::npos) {
            size_t end = pos + strlen(mark);
            if (split == std::string::npos || end > split) split = end;
        }
    }

    // 无强边界时用逗号：首句 8 个汉字即切（快出声），后续 15 个汉字才切（避免切碎）
    if (split == std::string::npos) {
        size_t comma_threshold = is_first ? 24 : 45;
        if (buffer.size() >= comma_threshold) {
            size_t pos = buffer.rfind("，");
            if (pos != std::string::npos) split = pos + 3;
        }
    }

    if (split == std::string::npos || split < 6) return "";  // 至少 2 个汉字
    std::string sentence = buffer.substr(0, split);
    buffer.erase(0, split);
    return sentence;
}

AvvtnCapture* AvvtnCapture::g_avvtn_capture_instance = nullptr;

// 初始化
int AvvtnCapture::Init(std::string avvtn_cfg_path)
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

    // 2、初始化 SenseVoice 本地 ASR
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

    // 3、初始化 Matcha TTS
    LOG_INFO("初始化 Matcha TTS");
    std::string tts_model_dir = project_dir + "/matcha-icefall-zh-baker";
    std::string vocoder_path = tts_model_dir + "/hifigan_v2.onnx";
    ret = sherpa_tts_.Init(tts_model_dir, vocoder_path);
    if (ret != 0)
    {
        LOG_ERROR("初始化 TTS 失败");
    }
    else
    {
        LOG_INFO("初始化 TTS 成功");
    }

    // 4、初始化音频播放器
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

    // 4.1、初始化 LLM 客户端
    LOG_INFO("初始化 LLM 客户端");

    const std::string prompt_dir = "/home/jetson/local_cmcc_robot/resource/prompts";

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
    ret = llm_chat_.Init("http://127.0.0.1:8082");
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
        LOG_INFO("初始化对话 LLM 成功 (port 8082)");
    }

    // 远程闲聊模型
    ret = llm_remote_.Init("http://10.33.225.63:9090/agent/llm/openai", "qwen");
    if (ret != 0)
    {
        LOG_ERROR("初始化远程闲聊 LLM 失败");
    }
    else
    {
        std::string chat_prompt = ReadPromptFile(prompt_dir + "/chat.prompt");
        if (!chat_prompt.empty()) {
            llm_remote_.SetSystemPrompt(chat_prompt);
        }
        llm_remote_.SetParams(0.7f, 2048);
        LOG_INFO("初始化远程闲聊 LLM 成功 (http://10.33.225.63:9090)");
    }

    LOG_INFO("闲聊模型选择: %s", USE_REMOTE_LLM ? "远程 qwen" : "本地 qwen3-8b");

    // 5、初始化音频采集
    LOG_INFO("初始化音频采集");
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
    }

    // test_avvtn();
    return 0;
}

// 销毁
int AvvtnCapture::Destory()
{
    int ret = 0;

    // 清空全局实例指针，后续回调会因空指针检查而跳过
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

    return 0;
}

// TTS 语音合成并播放
void AvvtnCapture::Speak(const std::string& text, float speed, bool append_mode, uint64_t session_id)
{
    if (!sherpa_tts_.IsInitialized()) {
        LOG_WARN("TTS 未初始化，无法合成语音");
        return;
    }

    if (text.empty()) {
        LOG_WARN("TTS 输入文本为空");
        return;
    }

    // 检查是否为中文内容（包含汉字或中文标点）
    // 规则：有非 ASCII 字符且不含拉丁字母 → 中文；含拉丁字母则检查是否有汉字
    bool has_chinese = false;
    bool has_non_ascii = false;
    bool has_latin = false;
    bool has_cjk = false;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= 0x80) {
            has_non_ascii = true;
            if (c >= 0xE4 && c <= 0xE9) has_cjk = true;  // CJK 汉字范围
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            has_latin = true;
        }
    }
    if (has_non_ascii && !has_latin) {
        has_chinese = true;       // 纯中文/中文标点，无英文
    } else if (has_cjk) {
        has_chinese = true;       // 混合内容但含汉字
    }

    if (!has_chinese) {
        LOG_WARN("TTS 跳过非中文文本: \"%s\"", text.c_str());
        return;
    }

    LOG_INFO("TTS 播放: \"%s\"", text.c_str());

    // 直接调用（非 ChatAndSpeak 流式）：打断当前播放并使旧 LLM 会话失效，
    // 防止旧 LLM 流的 stream_cb 继续推送音频到队列
    if (!append_mode) {
        if (session_id == 0) {
            // 外部直接 TTS：使旧 LLM 回调失效
            ++chat_session_id_;
        }
        audio_player_.Interrupt();
    }

    // 记录当前会话号，合成期间若会话变更则中止（新提问或新直接 TTS 到达）
    uint64_t my_session = chat_session_id_.load();

    // 流式合成：边合成边播放
    sherpa_tts_.Generate(text, [this, my_session](const AudioChunk& chunk, float progress) -> bool {
        // 会话已变更（新的提问或直接 TTS 打断）：中止合成，不再推送
        if (chat_session_id_.load() != my_session) return false;
        audio_player_.StreamPush(chunk.samples.data(), chunk.samples.size());
        return true;  // 继续合成
    }, speed);

    // 通知播放结束（追加模式不结束，由调用方控制）
    if (!append_mode) {
        // 会话已变更时不要设置 stream_ended_（新会话自己管理）
        if (chat_session_id_.load() == my_session) {
            audio_player_.StreamEnd();
        }
    }
}

void AvvtnCapture::ChatAndSpeak(const std::string& text)
{
#if USE_REMOTE_LLM
    LOG_INFO("远程 LLM 闲聊调用: \"%s\"", text.c_str());
#else
    LOG_INFO("本地 LLM 闲聊调用: \"%s\"", text.c_str());
#endif

    // 新提问到达：会话号自增使旧 LLM 回调失效，并立即打断清空正在播放的旧回答
    uint64_t session_id = ++chat_session_id_;
    audio_player_.Interrupt();

    auto sentence_buffer = std::make_shared<std::string>();
    auto is_first_synthesis = std::make_shared<bool>(true);

    // 流式回调：句子级 TTS 缓冲（精确按标点位置切句）
    auto stream_cb = [this, sentence_buffer, is_first_synthesis, session_id](const std::string& chunk, bool is_done) -> bool {
        // 会话已过期（用户有了新提问）：丢弃并中止旧 LLM 流
        if (chat_session_id_.load() != session_id) {
            LOG_WARN("丢弃过期 LLM 流式数据 (session {}, current {})", session_id, chat_session_id_.load());
            return false;
        }
        if (!chunk.empty()) {
            *sentence_buffer += chunk;
            // 循环提取：一个 chunk 可能带出多个完整句
            while (true) {
                std::string sentence = ExtractCompleteSentence(*sentence_buffer, *is_first_synthesis);
                if (sentence.empty()) break;
                sentence = CleanForTts(sentence);
                LOG_INFO("LLM 句子: \"%s\" (first=%d)", sentence.c_str(), *is_first_synthesis);
                bool append = !(*is_first_synthesis);
                Speak(sentence, 1.0f, append, session_id);
                *is_first_synthesis = false;
            }
        }
        return true;
    };

    // 完成回调：发布 ROS 消息 + 合成剩余缓冲
    auto complete_cb = [this, sentence_buffer, is_first_synthesis, session_id](const std::string& full_response) {
        // 会话已过期：丢弃，不再合成剩余文本也不发布消息
        if (chat_session_id_.load() != session_id) {
            LOG_WARN("丢弃过期 LLM 完成回调 (session {}, current {})", session_id, chat_session_id_.load());
            return;
        }
        LOG_INFO("LLM 回复: %s", full_response.c_str());

        if (full_response.empty()) {
            LOG_WARN("LLM 回复为空，跳过发布");
            audio_player_.StreamEnd();
            return;
        }

        nlohmann::json chat_llm = {
            {"speaker", "ROBOT"},
            {"content", full_response}
        };
        ROSManager::getInstance().publishChatConversation(chat_llm.dump());

        if (!sentence_buffer->empty()) {
            std::string sentence = CleanForTts(*sentence_buffer);
            LOG_INFO("LLM 句子(剩余): \"%s\"", sentence.c_str());
            bool append = !(*is_first_synthesis);
            Speak(sentence, 1.0f, append, session_id);
        }
        audio_player_.StreamEnd();
    };

#if USE_REMOTE_LLM
    llm_remote_.ChatAsync(text, stream_cb, complete_cb);
#else
    llm_chat_.ChatAsync(text, stream_cb, complete_cb);
#endif
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