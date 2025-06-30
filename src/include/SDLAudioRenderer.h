#pragma once

#include "IAudioRenderer.h"
#include <SDL2/SDL.h>
#include <mutex>
#include <atomic>

// 前向声明 FFmpeg 类型
extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}

class SDLAudioRenderer : public IAudioRenderer {
public:
    SDLAudioRenderer();
    virtual ~SDLAudioRenderer() override;

    // 禁用拷贝构造和赋值
    SDLAudioRenderer(const SDLAudioRenderer&) = delete;
    SDLAudioRenderer& operator=(const SDLAudioRenderer&) = delete;

    // IAudioRenderer 接口实现
    bool init(int sampleRate, int channels, enum AVSampleFormat decoderSampleFormat,
        AVRational timeBase, IClockManager* clockManager) override;
    bool renderFrame(AVFrame* frame, const std::atomic<bool>& quit) override;
    void play() override;
    void pause() override;
    void flushBuffers() override;
    void close() override;

private:
    SDL_AudioDeviceID m_audio_device_id = 0;
    SDL_AudioSpec m_actual_spec; // SDL实际打开的音频规格

    // 重采样相关
    SwrContext* m_swr_context = nullptr;
    uint8_t* m_resampled_buffer = nullptr; // 重采样后的数据缓冲区
    unsigned int m_resampled_buffer_size = 0; // 缓冲区大小

    // 目标音频参数
    int m_target_channels = 0;
    enum AVSampleFormat m_target_sample_fmt = AV_SAMPLE_FMT_S16;

    // 同步相关
    IClockManager* m_clock_manager = nullptr;
    AVRational m_time_base;
    int m_bytes_per_second = 0;
};
