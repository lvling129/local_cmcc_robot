#include "audio_player.h"
#include "utils/Logger.hpp"

#include <alsa/asoundlib.h>
#include <algorithm>
#include <cmath>

AudioPlayer::~AudioPlayer()
{
    Destroy();
}

int AudioPlayer::Init(const std::string& device, int sample_rate)
{
    if (pcm_) {
        LOG_WARN("AudioPlayer 已初始化");
        return 0;
    }

    sample_rate_ = sample_rate;

    int err = snd_pcm_open(&pcm_, device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        LOG_ERROR("打开 ALSA 设备失败: %s", snd_strerror(err));
        return -1;
    }

    // 配置硬件参数
    snd_pcm_hw_params_t* hw_params;
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(pcm_, hw_params);

    snd_pcm_hw_params_set_access(pcm_, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_, hw_params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm_, hw_params, channels_);

    unsigned int rate = sample_rate_;
    snd_pcm_hw_params_set_rate_near(pcm_, hw_params, &rate, nullptr);
    sample_rate_ = rate;

    // 设置缓冲大小（约 100ms）
    snd_pcm_uframes_t buffer_size = sample_rate_ / 10;
    snd_pcm_hw_params_set_buffer_size_near(pcm_, hw_params, &buffer_size);

    // 设置 period（约 20ms）
    snd_pcm_uframes_t period_size = sample_rate_ / 50;
    snd_pcm_hw_params_set_period_size_near(pcm_, hw_params, &period_size, nullptr);

    err = snd_pcm_hw_params(pcm_, hw_params);
    if (err < 0) {
        LOG_ERROR("设置硬件参数失败: %s", snd_strerror(err));
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return -1;
    }

    // 软件参数
    snd_pcm_sw_params_t* sw_params;
    snd_pcm_sw_params_alloca(&sw_params);
    snd_pcm_sw_params_current(pcm_, sw_params);
    snd_pcm_sw_params_set_start_threshold(pcm_, sw_params, period_size);
    snd_pcm_sw_params_set_avail_min(pcm_, sw_params, period_size);

    err = snd_pcm_sw_params(pcm_, sw_params);
    if (err < 0) {
        LOG_ERROR("设置软件参数失败: %s", snd_strerror(err));
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return -1;
    }

    err = snd_pcm_prepare(pcm_);
    if (err < 0) {
        LOG_ERROR("准备 PCM 失败: %s", snd_strerror(err));
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return -1;
    }

    // 启动播放线程
    running_ = true;
    playback_thread_ = std::thread(&AudioPlayer::PlaybackThread, this);

    LOG_INFO("AudioPlayer 初始化成功: %s, %d Hz, %d 通道", device.c_str(), sample_rate_, channels_);
    return 0;
}

int AudioPlayer::Play(const float* samples, int num_samples)
{
    if (!pcm_ || !samples || num_samples <= 0) return -1;

    // 打断当前播放
    Interrupt();

    // 清空队列并添加新音频
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!audio_queue_.empty()) audio_queue_.pop();
        stream_ended_ = false;

        // 转换为 int16
        std::vector<int16_t> pcm_data(num_samples);
        for (int i = 0; i < num_samples; ++i) {
            float s = std::max(-1.0f, std::min(1.0f, samples[i]));
            pcm_data[i] = static_cast<int16_t>(s * 32767.0f);
        }
        audio_queue_.push(std::move(pcm_data));
        stream_ended_ = true;
    }
    queue_cv_.notify_one();

    playing_ = true;
    return 0;
}

int AudioPlayer::StreamPush(const float* samples, int num_samples)
{
    if (!pcm_ || !samples || num_samples <= 0) return -1;

    // 转换为 int16
    std::vector<int16_t> pcm_data(num_samples);
    for (int i = 0; i < num_samples; ++i) {
        float s = std::max(-1.0f, std::min(1.0f, samples[i]));
        pcm_data[i] = static_cast<int16_t>(s * 32767.0f);
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        audio_queue_.push(std::move(pcm_data));
        if (!playing_) {
            playing_ = true;
        }
    }
    queue_cv_.notify_one();

    return 0;
}

void AudioPlayer::StreamEnd()
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stream_ended_ = true;
    }
    queue_cv_.notify_one();
}

void AudioPlayer::Interrupt()
{
    // 自增打断计数（只增不减）：播放线程正在写入的旧音频块会被废弃。
    // 不能用"置位后立刻复位"的布尔标志——播放线程阻塞在 snd_pcm_writei 时，
    // 复位后它复查会误判为未打断，把旧块重新写入 ALSA（旧回答"复活"）
    ++interrupt_count_;

    // 清空队列
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!audio_queue_.empty()) audio_queue_.pop();
        stream_ended_ = true;
    }
    queue_cv_.notify_one();

    // 重置 PCM：丢弃硬件缓冲中的剩余音频
    if (pcm_) {
        snd_pcm_drop(pcm_);
        snd_pcm_prepare(pcm_);
    }

    playing_ = false;
}

void AudioPlayer::Wait()
{
    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_cv_.wait(lock, [this]() {
        return audio_queue_.empty() && stream_ended_;
    });
}

void AudioPlayer::PlaybackThread()
{
    while (running_) {
        std::vector<int16_t> pcm_data;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return !audio_queue_.empty() || stream_ended_ || !running_;
            });

            if (!running_) break;
            if (audio_queue_.empty()) {
                if (stream_ended_) {
                    playing_ = false;
                }
                continue;
            }

            pcm_data = std::move(audio_queue_.front());
            audio_queue_.pop();
        }

        // 取块时记录打断计数：写入期间一旦发生 Interrupt，本块立即废弃
        uint32_t ic = interrupt_count_.load();

        // 写入 ALSA（int16 格式）
        if (!pcm_data.empty() && pcm_ && ic == interrupt_count_.load()) {
            int written = 0;
            int total = pcm_data.size();
            while (written < total && ic == interrupt_count_.load()) {
                int frames = snd_pcm_writei(pcm_, pcm_data.data() + written, total - written);
                if (frames < 0) {
                    frames = snd_pcm_recover(pcm_, frames, 0);
                    if (frames < 0) {
                        LOG_ERROR("ALSA 写入失败: %s", snd_strerror(frames));
                        break;
                    }
                }
                written += frames;
            }
        }

        // 检查是否播放完成
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (audio_queue_.empty() && stream_ended_) {
                playing_ = false;
            }
        }
    }
}

void AudioPlayer::Destroy()
{
    running_ = false;
    queue_cv_.notify_all();

    if (playback_thread_.joinable()) {
        playback_thread_.join();
    }

    if (pcm_) {
        snd_pcm_drop(pcm_);
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
    }

    LOG_INFO("AudioPlayer 已销毁");
}
