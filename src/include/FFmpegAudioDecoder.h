#pragma once

#include "IAudioDecoder.h"
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

class FFmpegAudioDecoder : public IAudioDecoder {
private:
    AVCodecContext* m_codecContext = nullptr;
    const AVCodec* m_codec = nullptr;
    IClockManager* m_clockManager = nullptr; // Ŀǰδʹ�ã���Ϊ���ֽӿ�һ���Զ�����
    AVRational m_timeBase = { 0, 1 };

public:
    FFmpegAudioDecoder();
    virtual ~FFmpegAudioDecoder() override;

    // ���ÿ����͸�ֵ
    FFmpegAudioDecoder(const FFmpegAudioDecoder&) = delete;
    FFmpegAudioDecoder& operator=(const FFmpegAudioDecoder&) = delete;

    // IAudioDecoder �ӿ�ʵ��
    bool init(AVCodecParameters* codecParams, AVRational timeBase, IClockManager* clockManager) override;
    int decode(AVPacket* packet, AVFrame** frame) override;
    void close() override;

    int getSampleRate() const override;
    int getChannels() const override;
    enum AVSampleFormat getSampleFormat() const override;
    struct AVRational getTimeBase() const override;
    int getBytesPerSampleFrame() const override;
};
