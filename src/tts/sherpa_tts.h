#ifndef SHERPA_TTS_H
#define SHERPA_TTS_H

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

// 前向声明
struct SherpaOnnxOfflineTts;
struct SherpaOnnxGeneratedAudio;

/**
 * @brief 音频数据块
 */
struct AudioChunk {
    std::vector<float> samples;  // float [-1, 1] 音频样本
    int sample_rate;             // 采样率
};

/**
 * @brief TTS 生成进度回调
 * @param chunk 新生成的音频块
 * @param progress 进度 [0, 1]
 * @return true 继续生成，false 取消生成
 */
using TtsProgressCallback = std::function<bool(const AudioChunk& chunk, float progress)>;

/**
 * @brief sherpa-onnx Matcha TTS 离线语音合成封装类
 *
 * 基于 Matcha-Icefall 中文模型，提供本地离线语音合成能力。
 * 支持流式回调输出音频块，适合边合成边播放场景。
 *
 * 工作流程：
 * 1. Init() 初始化 TTS 引擎
 * 2. Generate() 合成语音，通过回调接收音频块
 * 3. Destroy() 释放资源
 */
class SherpaTts {
public:
    SherpaTts() = default;
    ~SherpaTts();

    /**
     * @brief 初始化 TTS 引擎
     * @param model_dir 模型目录路径（包含 model-steps-3.onnx, lexicon.txt, tokens.txt, dict/）
     * @param vocoder_path vocoder 模型路径（vocos-22khz-univ.onnx）
     * @param num_threads ONNX Runtime 线程数，默认 2
     * @return 0 成功，非 0 失败
     */
    int Init(const std::string& model_dir, const std::string& vocoder_path, int num_threads = 2);

    /**
     * @brief 合成语音
     * @param text 要合成的文本
     * @param callback 进度回调，接收音频块和进度
     * @param speed 语速，默认 1.0（<1 慢，>1 快）
     * @return 0 成功，非 0 失败
     */
    int Generate(const std::string& text, TtsProgressCallback callback, float speed = 1.0f);

    /**
     * @brief 合成语音并返回完整音频
     * @param text 要合成的文本
     * @param speed 语速，默认 1.0
     * @return 完整音频数据，失败返回空 AudioChunk
     */
    AudioChunk GenerateFull(const std::string& text, float speed = 1.0f);

    /**
     * @brief 获取输出采样率
     */
    int GetSampleRate() const { return sample_rate_; }

    /**
     * @brief 检查是否已初始化
     */
    bool IsInitialized() const { return initialized_; }

    /**
     * @brief 销毁引擎，释放资源
     */
    void Destroy();

private:
    bool initialized_ = false;
    const SherpaOnnxOfflineTts* tts_ = nullptr;
    int sample_rate_ = 22050;  // Matcha 默认输出采样率
    std::string rule_fsts_;    // 规则 FST 列表
};

#endif // SHERPA_TTS_H
