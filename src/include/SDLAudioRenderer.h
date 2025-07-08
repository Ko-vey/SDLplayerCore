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

#include "IAudioRenderer.h"
#include <SDL2/SDL.h>
#include <mutex>
#include <atomic>

// ǰ������ FFmpeg ����
extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}

class SDLAudioRenderer : public IAudioRenderer {
public:
    SDLAudioRenderer();
    virtual ~SDLAudioRenderer() override;

    // ���ÿ�������͸�ֵ
    SDLAudioRenderer(const SDLAudioRenderer&) = delete;
    SDLAudioRenderer& operator=(const SDLAudioRenderer&) = delete;

    // IAudioRenderer �ӿ�ʵ��
    bool init(int sampleRate, int channels, enum AVSampleFormat decoderSampleFormat,
        AVRational timeBase, IClockManager* clockManager) override;
    bool renderFrame(AVFrame* frame, const std::atomic<bool>& quit) override;
    void play() override;
    void pause() override;
    void flushBuffers() override;
    void close() override;

private:
    SDL_AudioDeviceID m_audio_device_id = 0;
    SDL_AudioSpec m_actual_spec; // SDLʵ�ʴ򿪵���Ƶ���

    // �ز������
    SwrContext* m_swr_context = nullptr;
    uint8_t* m_resampled_buffer = nullptr; // �ز���������ݻ�����
    unsigned int m_resampled_buffer_size = 0; // ��������С

    // Ŀ����Ƶ����
    int m_target_channels = 0;
    enum AVSampleFormat m_target_sample_fmt = AV_SAMPLE_FMT_S16;

    // ͬ�����
    IClockManager* m_clock_manager = nullptr;
    AVRational m_time_base;
    int m_bytes_per_second = 0;
};
