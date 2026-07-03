#include "sherpa_asr.h"
#include "utils/Logger.hpp"

#include <cstring>
#include <sherpa-onnx/c-api/c-api.h>

SherpaAsr::~SherpaAsr()
{
    Destroy();
}

int SherpaAsr::Init(const std::string& model_dir, int num_threads)
{
    if (initialized_) {
        LOG_WARN("SherpaAsr 已初始化，跳过");
        return 0;
    }

    std::string model_path = model_dir + "/model.int8.onnx";
    std::string tokens_path = model_dir + "/tokens.txt";

    LOG_INFO("初始化 SenseVoice ASR");
    LOG_INFO("  model:  %s", model_path.c_str());
    LOG_INFO("  tokens: %s", tokens_path.c_str());

    SherpaOnnxOfflineRecognizerConfig config;
    memset(&config, 0, sizeof(config));

    // SenseVoice 模型配置
    config.model_config.sense_voice.model = model_path.c_str();
    config.model_config.sense_voice.language = "auto";
    config.model_config.sense_voice.use_itn = 1;
    config.model_config.tokens = tokens_path.c_str();
    config.model_config.num_threads = num_threads;
    config.model_config.provider = "cpu";
    config.model_config.debug = 0;

    // 解码配置
    config.decoding_method = "greedy_search";
    config.max_active_paths = 4;

    recognizer_ = SherpaOnnxCreateOfflineRecognizer(&config);
    if (!recognizer_) {
        LOG_ERROR("创建 SenseVoice 识别器失败");
        return -1;
    }

    initialized_ = true;
    LOG_INFO("SenseVoice ASR 初始化成功");
    return 0;
}

void SherpaAsr::FeedAudio(const int16_t* data, int len)
{
    if (!initialized_ || !data || len <= 0) return;

    // 只累积，等 FinalizeAudio 一次性处理
    int num_samples = len / 2;
    utterance_buffer_.insert(utterance_buffer_.end(), data, data + num_samples);
}

void SherpaAsr::FinalizeAudio()
{
    if (!initialized_ || !recognizer_ || utterance_buffer_.empty()) return;

    // 销毁上一次的离线流
    if (stream_) {
        SherpaOnnxDestroyOfflineStream(stream_);
        stream_ = nullptr;
    }

    // 创建离线流
    stream_ = SherpaOnnxCreateOfflineStream(recognizer_);
    if (!stream_) {
        LOG_ERROR("创建 OfflineStream 失败");
        return;
    }

    // S16LE → float
    int total = utterance_buffer_.size();
    float_buffer_.resize(total);
    for (int i = 0; i < total; ++i) {
        float_buffer_[i] = static_cast<float>(utterance_buffer_[i]) / 32768.0f;
    }

    // 一次性送入全部音频并解码
    SherpaOnnxAcceptWaveformOffline(stream_, 16000, float_buffer_.data(), total);
    SherpaOnnxDecodeOfflineStream(recognizer_, stream_);

    LOG_DEBUG("SenseVoice 解码完成，音频长度: %dms", (total * 1000) / 16000);
}

std::string SherpaAsr::GetResult()
{
    if (!initialized_ || !stream_) return "";

    const SherpaOnnxOfflineRecognizerResult* result =
        SherpaOnnxGetOfflineStreamResult(stream_);

    if (!result || !result->text) {
        if (result) SherpaOnnxDestroyOfflineRecognizerResult(result);
        return "";
    }

    // 打印 SenseVoice 额外字段
    LOG_INFO("SenseVoice 识别结果:");
    LOG_INFO("  文本:   %s", result->text[0] ? result->text : "(空)");
    LOG_INFO("  语言:   %s", result->lang ? result->lang : "(无)");
    LOG_INFO("  情感:   %s", result->emotion ? result->emotion : "(无)");
    LOG_INFO("  事件:   %s", result->event ? result->event : "(无)");

    std::string text(result->text);
    last_lang_ = (result->lang) ? result->lang : "";
    SherpaOnnxDestroyOfflineRecognizerResult(result);
    return text;
}

void SherpaAsr::Reset()
{
    if (!initialized_) return;

    // 销毁离线流，下次 FinalizeAudio 会重新创建
    if (stream_) {
        SherpaOnnxDestroyOfflineStream(stream_);
        stream_ = nullptr;
    }
    utterance_buffer_.clear();
}

void SherpaAsr::Destroy()
{
    if (stream_) {
        SherpaOnnxDestroyOfflineStream(stream_);
        stream_ = nullptr;
    }
    if (recognizer_) {
        SherpaOnnxDestroyOfflineRecognizer(recognizer_);
        recognizer_ = nullptr;
    }
    utterance_buffer_.clear();
    initialized_ = false;
    LOG_INFO("SenseVoice ASR 已销毁");
}
