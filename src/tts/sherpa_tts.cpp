#include "sherpa_tts.h"
#include "utils/Logger.hpp"

#include <cstring>
#include <sherpa-onnx/c-api/c-api.h>

SherpaTts::~SherpaTts()
{
    Destroy();
}

int SherpaTts::Init(const std::string& model_dir, const std::string& vocoder_path, int num_threads)
{
    if (initialized_) {
        LOG_WARN("SherpaTts 已初始化，跳过");
        return 0;
    }

    std::string acoustic_model = model_dir + "/model-steps-3.onnx";
    std::string lexicon = model_dir + "/lexicon.txt";
    std::string tokens = model_dir + "/tokens.txt";

    // 构建 rule_fsts
    rule_fsts_ = model_dir + "/phone.fst," + model_dir + "/date.fst," + model_dir + "/number.fst";

    LOG_INFO("初始化 Matcha TTS");
    LOG_INFO("  acoustic_model: %s", acoustic_model.c_str());
    LOG_INFO("  vocoder:      %s", vocoder_path.c_str());
    LOG_INFO("  lexicon:      %s", lexicon.c_str());
    LOG_INFO("  tokens:       %s", tokens.c_str());
    LOG_INFO("  rule_fsts:    %s", rule_fsts_.c_str());

    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));

    // Matcha 模型配置
    config.model.matcha.acoustic_model = acoustic_model.c_str();
    config.model.matcha.vocoder = vocoder_path.c_str();
    config.model.matcha.lexicon = lexicon.c_str();
    config.model.matcha.tokens = tokens.c_str();
    config.model.matcha.data_dir = nullptr;  // 中文模型不需要 espeak-ng-data
    config.model.matcha.noise_scale = 0.667f;
    config.model.matcha.length_scale = 1.0f;

    config.model.num_threads = num_threads;
    config.model.provider = "cpu";
    config.model.debug = 0;

    config.rule_fsts = rule_fsts_.c_str();
    config.max_num_sentences = 2;
    config.silence_scale = 0.2f;

    tts_ = SherpaOnnxCreateOfflineTts(&config);
    if (!tts_) {
        LOG_ERROR("创建 Matcha TTS 引擎失败");
        return -1;
    }

    sample_rate_ = SherpaOnnxOfflineTtsSampleRate(tts_);
    LOG_INFO("Matcha TTS 初始化成功，采样率: %d Hz", sample_rate_);

    initialized_ = true;
    return 0;
}

int SherpaTts::Generate(const std::string& text, TtsProgressCallback callback, float speed)
{
    if (!initialized_ || !tts_) {
        LOG_ERROR("TTS 未初始化");
        return -1;
    }

    if (text.empty()) {
        LOG_WARN("TTS 输入文本为空");
        return 0;
    }

    LOG_INFO("TTS 合成: \"%s\" (speed=%.2f)", text.c_str(), speed);

    // 用于传递回调和状态的结构体
    struct CallbackContext {
        TtsProgressCallback user_callback;
        int sample_rate;
        bool cancelled;
    };
    CallbackContext ctx{callback, sample_rate_, false};

    // C 风格回调函数
    auto c_callback = [](const float* samples, int32_t n, float progress, void* arg) -> int32_t {
        auto* ctx = static_cast<CallbackContext*>(arg);
        if (ctx->cancelled) return 0;

        AudioChunk chunk;
        chunk.samples.assign(samples, samples + n);
        chunk.sample_rate = ctx->sample_rate;

        bool cont = true;
        if (ctx->user_callback) {
            cont = ctx->user_callback(chunk, progress);
        }
        if (!cont) {
            ctx->cancelled = true;
            return 0;
        }
        return 1;
    };

    SherpaOnnxGenerationConfig gen_config;
    memset(&gen_config, 0, sizeof(gen_config));
    gen_config.speed = speed;
    gen_config.sid = 0;
    gen_config.silence_scale = 0.2f;

    const SherpaOnnxGeneratedAudio* audio = SherpaOnnxOfflineTtsGenerateWithConfig(
        tts_, text.c_str(), &gen_config, c_callback, &ctx);

    if (!audio) {
        LOG_ERROR("TTS 合成失败");
        return -1;
    }

    LOG_INFO("TTS 合成完成，共 %d 个样本 (%.2f 秒)",
             audio->n, static_cast<float>(audio->n) / audio->sample_rate);

    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    return 0;
}

AudioChunk SherpaTts::GenerateFull(const std::string& text, float speed)
{
    AudioChunk result;
    result.sample_rate = sample_rate_;

    if (!initialized_ || !tts_ || text.empty()) {
        return result;
    }

    SherpaOnnxGenerationConfig gen_config;
    memset(&gen_config, 0, sizeof(gen_config));
    gen_config.speed = speed;
    gen_config.sid = 0;
    gen_config.silence_scale = 0.2f;

    const SherpaOnnxGeneratedAudio* audio = SherpaOnnxOfflineTtsGenerateWithConfig(
        tts_, text.c_str(), &gen_config, nullptr, nullptr);

    if (audio) {
        result.samples.assign(audio->samples, audio->samples + audio->n);
        SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    }

    return result;
}

void SherpaTts::Destroy()
{
    if (tts_) {
        SherpaOnnxDestroyOfflineTts(tts_);
        tts_ = nullptr;
    }
    initialized_ = false;
    LOG_INFO("Matcha TTS 已销毁");
}
