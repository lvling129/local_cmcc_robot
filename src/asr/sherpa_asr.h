#ifndef SHERPA_ASR_H
#define SHERPA_ASR_H

#include <string>
#include <vector>
#include <cstdint>

// 前向声明，避免在头文件中包含 sherpa-onnx 头文件
struct SherpaOnnxOnlineRecognizer;
struct SherpaOnnxOnlineStream;
struct SherpaOnnxOfflineRecognizer;
struct SherpaOnnxOfflineStream;

/**
 * @brief 支持的 ASR 模型类型
 */
enum class AsrModelType {
    kOnlineParaformer,     // 流式 Paraformer（实时中间结果）
    kOfflineSenseVoice,    // 离线 SenseVoice（整句解码，精度更高）
};

/**
 * @brief sherpa-onnx ASR 封装类
 *
 * 同时支持流式 Paraformer 和离线 SenseVoice 两种模型。
 * 通过 Init() 参数选择使用哪个模型，运行时不可切换。
 *
 * 输入：16kHz mono S16LE PCM 音频
 * 输出：识别文本
 */
class SherpaAsr
{
public:
    SherpaAsr() = default;
    ~SherpaAsr();

    /**
     * @brief 初始化识别器
     * @param model_dir 模型目录路径
     * @param type 模型类型：kOnlineParaformer 或 kOfflineSenseVoice
     * @param num_threads ONNX Runtime 线程数，默认 2
     * @return 0 成功，非 0 失败
     */
    int Init(const std::string& model_dir,
             AsrModelType type = AsrModelType::kOnlineParaformer,
             int num_threads = 2);

    /**
     * @brief 喂入音频数据
     * @param data S16LE 音频数据指针
     * @param len 数据长度（字节数）
     *
     * Paraformer：累积 320ms 缓冲后送入识别器
     * SenseVoice：只累积到 utterance_buffer_，等待 FinalizeAudio 一次性处理
     */
    void FeedAudio(const int16_t* data, int len);

    /**
     * @brief 结束当前句子的音频输入
     *
     * Paraformer：刷缓冲 + 发静音 + InputFinished + 解码
     * SenseVoice：将整句音频一次性送入并解码
     * 应在 GetResult() 和 Reset() 之前调用。
     */
    void FinalizeAudio();

    /**
     * @brief 获取当前识别结果
     * @return 当前累积的识别文本
     */
    std::string GetResult();

    /**
     * @brief 检测是否到达端点（仅 Paraformer 有效）
     * @return true 表示到达端点
     */
    bool IsEndpoint();

    /**
     * @brief 重置识别状态（一句话结束后调用，开始下一句）
     */
    void Reset();

    /**
     * @brief 销毁识别器，释放资源
     */
    void Destroy();

    /**
     * @brief 检查是否已初始化
     */
    bool IsInitialized() const { return initialized_; }

    /**
     * @brief 获取当前使用的模型类型
     */
    AsrModelType GetModelType() const { return model_type_; }

private:
    // 通用
    bool initialized_ = false;
    AsrModelType model_type_ = AsrModelType::kOnlineParaformer;

    // Paraformer（流式）
    const SherpaOnnxOnlineRecognizer* recognizer_ = nullptr;
    const SherpaOnnxOnlineStream* stream_ = nullptr;
    std::vector<float> float_buffer_;    // S16LE → float 转换缓冲
    std::vector<int16_t> pcm_buffer_;    // S16LE 累积缓冲（320ms = 5120 样本）
    static constexpr int kBufferThreshold = 5120;  // 320ms at 16kHz

    // SenseVoice（离线）
    const SherpaOnnxOfflineRecognizer* offline_recognizer_ = nullptr;
    const SherpaOnnxOfflineStream* offline_stream_ = nullptr;
    std::vector<int16_t> utterance_buffer_;  // 累积整句 S16LE 音频

    // 内部初始化函数
    int InitParaformer(const std::string& model_dir, int num_threads);
    int InitSenseVoice(const std::string& model_dir, int num_threads);
};

#endif // SHERPA_ASR_H
