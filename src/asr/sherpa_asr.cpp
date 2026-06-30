#include "sherpa_asr.h"
#include "utils/Logger.hpp"

#include <cstring>
#include <sherpa-onnx/c-api/c-api.h>

SherpaAsr::~SherpaAsr()
{
    Destroy();
}

int SherpaAsr::Init(const std::string& model_dir, AsrModelType type, int num_threads)
{
    if (initialized_) {
        LOG_WARN("SherpaAsr 已初始化，跳过");
        return 0;
    }

    model_type_ = type;

    int ret = 0;
    if (type == AsrModelType::kOfflineSenseVoice) {
        ret = InitSenseVoice(model_dir, num_threads);
    } else {
        ret = InitParaformer(model_dir, num_threads);
    }

    if (ret == 0) {
        initialized_ = true;
    }
    return ret;
}

int SherpaAsr::InitParaformer(const std::string& model_dir, int num_threads)
{
    std::string encoder_path = model_dir + "/encoder.int8.onnx";
    std::string decoder_path = model_dir + "/decoder.int8.onnx";
    std::string tokens_path  = model_dir + "/tokens.txt";

    LOG_INFO("初始化 sherpa-onnx ASR [Paraformer 流式]");
    LOG_INFO("  encoder: %s", encoder_path.c_str());
    LOG_INFO("  decoder: %s", decoder_path.c_str());
    LOG_INFO("  tokens:  %s", tokens_path.c_str());

    SherpaOnnxOnlineRecognizerConfig config;
    memset(&config, 0, sizeof(config));

    // 特征配置
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;

    // Paraformer 模型配置
    config.model_config.paraformer.encoder = encoder_path.c_str();
    config.model_config.paraformer.decoder = decoder_path.c_str();
    config.model_config.tokens = tokens_path.c_str();
    config.model_config.num_threads = num_threads;
    config.model_config.provider = "cpu";
    config.model_config.debug = 0;

    // 解码配置
    config.decoding_method = "greedy_search";
    config.max_active_paths = 4;

    // 端点检测：关闭，由外部 AVVTN vad_status 控制
    config.enable_endpoint = 0;
    config.rule1_min_trailing_silence = 0;
    config.rule2_min_trailing_silence = 0;
    config.rule3_min_utterance_length = 0;

    recognizer_ = SherpaOnnxCreateOnlineRecognizer(&config);
    if (!recognizer_) {
        LOG_ERROR("创建 sherpa-onnx Online 识别器失败");
        return -1;
    }

    stream_ = SherpaOnnxCreateOnlineStream(recognizer_);
    if (!stream_) {
        LOG_ERROR("创建 sherpa-onnx Online 流失败");
        SherpaOnnxDestroyOnlineRecognizer(recognizer_);
        recognizer_ = nullptr;
        return -1;
    }

    LOG_INFO("sherpa-onnx ASR [Paraformer] 初始化成功");
    return 0;
}

int SherpaAsr::InitSenseVoice(const std::string& model_dir, int num_threads)
{
    // SenseVoice 使用 model.int8.onnx（较小）或 model.onnx（完整精度）
    std::string model_path = model_dir + "/model.int8.onnx";
    std::string tokens_path = model_dir + "/tokens.txt";

    LOG_INFO("初始化 sherpa-onnx ASR [SenseVoice 离线]");
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

    offline_recognizer_ = SherpaOnnxCreateOfflineRecognizer(&config);
    if (!offline_recognizer_) {
        LOG_ERROR("创建 sherpa-onnx Offline 识别器失败");
        return -1;
    }

    LOG_INFO("sherpa-onnx ASR [SenseVoice] 初始化成功");
    return 0;
}

void SherpaAsr::FeedAudio(const int16_t* data, int len)
{
    if (!initialized_ || !data || len <= 0) return;

    // 计算样本数：len 字节 / 2 字节每样本
    int num_samples = len / 2;

    if (model_type_ == AsrModelType::kOfflineSenseVoice) {
        // SenseVoice：只累积，不送入识别器，等 FinalizeAudio 一次性处理
        utterance_buffer_.insert(utterance_buffer_.end(), data, data + num_samples);
        return;
    }

    // Paraformer：累积到 320ms 再一次性送入
    if (!recognizer_ || !stream_) return;

    pcm_buffer_.insert(pcm_buffer_.end(), data, data + num_samples);

    if ((int)pcm_buffer_.size() >= kBufferThreshold) {
        int total = pcm_buffer_.size();
        float_buffer_.resize(total);

        // S16LE → float [-1, 1]
        for (int i = 0; i < total; ++i) {
            float_buffer_[i] = static_cast<float>(pcm_buffer_[i]) / 32768.0f;
        }

        // 送入识别器
        SherpaOnnxOnlineStreamAcceptWaveform(stream_, 16000,
                                              float_buffer_.data(), total);

        // 解码所有可用帧
        while (SherpaOnnxIsOnlineStreamReady(recognizer_, stream_)) {
            SherpaOnnxDecodeOnlineStream(recognizer_, stream_);
        }

        pcm_buffer_.clear();
    }
}

void SherpaAsr::FinalizeAudio()
{
    if (!initialized_) return;

    if (model_type_ == AsrModelType::kOfflineSenseVoice) {
        if (!offline_recognizer_ || utterance_buffer_.empty()) return;

        // 创建离线流
        if (offline_stream_) {
            SherpaOnnxDestroyOfflineStream(offline_stream_);
            offline_stream_ = nullptr;
        }
        offline_stream_ = SherpaOnnxCreateOfflineStream(offline_recognizer_);
        if (!offline_stream_) {
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
        SherpaOnnxAcceptWaveformOffline(offline_stream_, 16000,
                                        float_buffer_.data(), total);
        SherpaOnnxDecodeOfflineStream(offline_recognizer_, offline_stream_);

        LOG_DEBUG("SenseVoice 解码完成，音频长度: %dms", (total * 1000) / 16000);
        return;
    }

    // Paraformer
    if (!recognizer_ || !stream_) return;

    // 先刷出缓冲区中剩余的音频数据
    if (!pcm_buffer_.empty()) {
        int total = pcm_buffer_.size();
        float_buffer_.resize(total);
        for (int i = 0; i < total; ++i) {
            float_buffer_[i] = static_cast<float>(pcm_buffer_[i]) / 32768.0f;
        }
        SherpaOnnxOnlineStreamAcceptWaveform(stream_, 16000,
                                              float_buffer_.data(), total);
        pcm_buffer_.clear();
    }

    // 发送 500ms 静音音频（16kHz * 0.5s = 8000 个样本）
    const int silence_samples = 8000;
    std::vector<float> silence(silence_samples, 0.0f);
    SherpaOnnxOnlineStreamAcceptWaveform(stream_, 16000,
                                          silence.data(), silence_samples);

    // 通知识别器音频输入结束
    SherpaOnnxOnlineStreamInputFinished(stream_);

    // 解码剩余帧
    while (SherpaOnnxIsOnlineStreamReady(recognizer_, stream_)) {
        SherpaOnnxDecodeOnlineStream(recognizer_, stream_);
    }
}

std::string SherpaAsr::GetResult()
{
    if (!initialized_) return "";

    if (model_type_ == AsrModelType::kOfflineSenseVoice) {
        if (!offline_stream_) return "";

        const SherpaOnnxOfflineRecognizerResult* result =
            SherpaOnnxGetOfflineStreamResult(offline_stream_);

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
        SherpaOnnxDestroyOfflineRecognizerResult(result);
        return text;
    }

    // Paraformer
    if (!recognizer_ || !stream_) return "";

    const SherpaOnnxOnlineRecognizerResult* result =
        SherpaOnnxGetOnlineStreamResult(recognizer_, stream_);

    if (!result || !result->text) {
        if (result) SherpaOnnxDestroyOnlineRecognizerResult(result);
        return "";
    }

    std::string text(result->text);
    SherpaOnnxDestroyOnlineRecognizerResult(result);
    return text;
}

bool SherpaAsr::IsEndpoint()
{
    if (!initialized_ || model_type_ != AsrModelType::kOnlineParaformer) return false;
    if (!recognizer_ || !stream_) return false;
    return SherpaOnnxOnlineStreamIsEndpoint(recognizer_, stream_) != 0;
}

void SherpaAsr::Reset()
{
    if (!initialized_) return;

    if (model_type_ == AsrModelType::kOfflineSenseVoice) {
        // 销毁离线流，下次 FinalizeAudio 会重新创建
        if (offline_stream_) {
            SherpaOnnxDestroyOfflineStream(offline_stream_);
            offline_stream_ = nullptr;
        }
        utterance_buffer_.clear();
        return;
    }

    // Paraformer
    if (!recognizer_ || !stream_) return;
    SherpaOnnxOnlineStreamReset(recognizer_, stream_);
    pcm_buffer_.clear();
}

void SherpaAsr::Destroy()
{
    // SenseVoice 清理
    if (offline_stream_) {
        SherpaOnnxDestroyOfflineStream(offline_stream_);
        offline_stream_ = nullptr;
    }
    if (offline_recognizer_) {
        SherpaOnnxDestroyOfflineRecognizer(offline_recognizer_);
        offline_recognizer_ = nullptr;
    }
    utterance_buffer_.clear();

    // Paraformer 清理
    if (stream_) {
        SherpaOnnxDestroyOnlineStream(stream_);
        stream_ = nullptr;
    }
    if (recognizer_) {
        SherpaOnnxDestroyOnlineRecognizer(recognizer_);
        recognizer_ = nullptr;
    }
    pcm_buffer_.clear();

    initialized_ = false;
    LOG_INFO("sherpa-onnx ASR 已销毁");
}
