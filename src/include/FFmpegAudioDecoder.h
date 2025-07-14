/*
 * SDLplayerCore - An audio/video player core.
 * Copyright (C) 2025 Kovey <zzwaaa0396@qq.com>
 *
 * This file is part of SDLplayerCore.
 *
 * SDLplayerCore is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

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
    IClockManager* m_clockManager = nullptr;
    AVRational m_timeBase = { 0, 1 };

public:
    FFmpegAudioDecoder();
    virtual ~FFmpegAudioDecoder() override;

    // 禁用拷贝和赋值
    FFmpegAudioDecoder(const FFmpegAudioDecoder&) = delete;
    FFmpegAudioDecoder& operator=(const FFmpegAudioDecoder&) = delete;

    // IAudioDecoder 接口实现
    bool init(AVCodecParameters* codecParams, AVRational timeBase, IClockManager* clockManager) override;
    int decode(AVPacket* packet, AVFrame** frame) override;
    void close() override;

    int getSampleRate() const override;
    int getChannels() const override;
    enum AVSampleFormat getSampleFormat() const override;
    struct AVRational getTimeBase() const override;
    int getBytesPerSampleFrame() const override;
};
