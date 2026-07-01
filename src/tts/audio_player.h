#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <cstdint>

// 前向声明 ALSA 类型
typedef struct _snd_pcm snd_pcm_t;

/**
 * @brief 音频播放器
 *
 * 支持：
 * - 流式播放（边合成边播放）
 * - 打断播放（新 TTS 音频打断之前的播放）
 * - 线程安全的队列播放
 *
 * 使用方法：
 * 1. Init() 初始化 ALSA 设备
 * 2. Play() 或 StreamPlay() 播放音频
 * 3. Interrupt() 打断当前播放
 * 4. Destroy() 释放资源
 */
class AudioPlayer {
public:
    AudioPlayer() = default;
    ~AudioPlayer();

    /**
     * @brief 初始化音频播放器
     * @param device ALSA 设备名，默认 "default"
     * @param sample_rate 采样率，默认 22050
     * @return 0 成功，非 0 失败
     */
    int Init(const std::string& device = "default", int sample_rate = 22050);

    /**
     * @brief 播放完整音频（阻塞）
     * @param samples float 音频样本 [-1, 1]
     * @param num_samples 样本数量
     * @return 0 成功，非 0 失败
     */
    int Play(const float* samples, int num_samples);

    /**
     * @brief 流式播放：将音频块加入队列（非阻塞）
     * @param samples float 音频样本 [-1, 1]
     * @param num_samples 样本数量
     * @return 0 成功，非 0 失败
     */
    int StreamPush(const float* samples, int num_samples);

    /**
     * @brief 通知流式播放结束
     */
    void StreamEnd();

    /**
     * @brief 打断当前播放，清空队列
     */
    void Interrupt();

    /**
     * @brief 等待播放完成
     */
    void Wait();

    /**
     * @brief 检查是否正在播放
     */
    bool IsPlaying() const { return playing_.load(); }

    /**
     * @brief 销毁播放器，释放资源
     */
    void Destroy();

private:
    void PlaybackThread();
    int WriteSamples(const float* samples, int num_samples);

    snd_pcm_t* pcm_ = nullptr;
    int sample_rate_ = 22050;
    int channels_ = 1;

    std::thread playback_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> playing_{false};
    std::atomic<bool> interrupted_{false};

    std::queue<std::vector<int16_t>> audio_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    bool stream_ended_ = false;
};

#endif // AUDIO_PLAYER_H
