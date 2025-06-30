#pragma once

#include <cstdint>
#include <atomic>
#include "IClockManager.h"

// 前向声明 FFmpeg 类型
struct AVFrame;
struct AVRational;
enum AVSampleFormat;

class IAudioRenderer {
public:
    virtual ~IAudioRenderer() = default;

    /**
     * @brief 初始化音频渲染器。
     * @param sampleRate 期望的播放采样率。
     * @param channels 期望的声道数。
     * @param decoderSampleFormat 解码器提供的采样格式。
     * @param timeBase 音频流的时间基，用于计算PTS。
     * @param clockManager 用于更新时间的时钟管理器指针。
     * @return 成功返回 true，失败返回 false。
     */
    virtual bool init(int sampleRate, int channels, enum AVSampleFormat decoderSampleFormat,
        AVRational timeBase, IClockManager* clockManager) = 0;

    /**
     * @brief 渲染一帧音频。
     * 该方法会进行必要的重采样，并将音频数据推送到播放队列。
     * 如果音频缓冲区已满，此方法可能会阻塞。
     * @param frame 要渲染的音频帧。
     * @param quit 用于指示退出线程的标志。
     * @return 成功返回 true，失败返回 false。
     */
    virtual bool renderFrame(AVFrame* frame, const std::atomic<bool>& quit) = 0;

    /**
     * @brief 开始或恢复音频播放。
     */
    virtual void play() = 0;

    /**
     * @brief 暂停音频播放。
     */
    virtual void pause() = 0;

    /**
     * @brief 清空所有已排队的音频数据。对Seek操作至关重要。
     */
    virtual void flushBuffers() = 0;

    /**
     * @brief 关闭音频渲染器并释放所有相关资源。
     */
    virtual void close() = 0;
};