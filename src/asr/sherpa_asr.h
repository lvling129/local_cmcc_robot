#ifndef SHERPA_ASR_H
#define SHERPA_ASR_H

#include <string>
#include <vector>
#include <cstdint>

// 前向声明，避免在头文件中包含 sherpa-onnx 头文件
struct SherpaOnnxOfflineRecognizer;
struct SherpaOnnxOfflineStream;

/**
 * @brief sherpa-onnx SenseVoice 离线 ASR 封装类
 *
 * 基于 SenseVoice 模型，提供本地离线语音识别能力。
 * 支持中/英/日/韩/粤语自动检测，支持情感/环境音识别。
 *
 * 工作流程：
 * 1. FeedAudio() 累积音频
 * 2. FinalizeAudio() 一次性送入并解码
 * 3. GetResult() 取识别结果
 * 4. Reset() 准备下一句
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
     * @param model_dir 模型目录路径（包含 model.onnx, tokens.txt）
     * @param num_threads ONNX Runtime 线程数，默认 2
     * @return 0 成功，非 0 失败
     */
    int Init(const std::string& model_dir, int num_threads = 2);

    /**
     * @brief 累积音频数据
     * @param data S16LE 音频数据指针
     * @param len 数据长度（字节数）
     *
     * 内部只累积到 utterance_buffer_，等待 FinalizeAudio 一次性处理。
     */
    void FeedAudio(const int16_t* data, int len);

    /**
     * @brief 结束当前句子的音频输入
     *
     * 将累积的整句音频一次性送入识别器并解码。
     * 应在 GetResult() 和 Reset() 之前调用。
     */
    void FinalizeAudio();

    /**
     * @brief 获取当前识别结果
     * @return 识别文本
     */
    std::string GetResult();

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

private:
    bool initialized_ = false;
    const SherpaOnnxOfflineRecognizer* recognizer_ = nullptr;
    const SherpaOnnxOfflineStream* stream_ = nullptr;
    std::vector<float> float_buffer_;      // S16LE → float 转换缓冲
    std::vector<int16_t> utterance_buffer_; // 累积整句 S16LE 音频
};

#endif // SHERPA_ASR_H
