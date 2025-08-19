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

#include "../include/SDLAudioRenderer.h"
#include <stdexcept>
#include <iostream>

extern "C" {
#include <libavutil/error.h>
}

SDLAudioRenderer::~SDLAudioRenderer() {
    close();
}

bool SDLAudioRenderer::init(int sampleRate, int channels, AVSampleFormat decoderSampleFormat,
    AVRational timeBase, IClockManager* clockManager) {
    // 防御性检查
    if (decoderSampleFormat == AV_SAMPLE_FMT_NONE || channels <= 0 || sampleRate <= 0) {
        std::cerr << "SDLAudioRenderer: init called with invalid audio parameters. "
            << "SampleFormat: " << decoderSampleFormat
            << ", Channels: " << channels
            << ", SampleRate: " << sampleRate << std::endl;
        return false; // 直接返回失败，阻止后续错误
    }

    if (m_audio_device_id != 0) {
        std::cerr << "SDLAudioRenderer: Already initialized." << std::endl;
        return true;
    }

    m_clock_manager = clockManager;
    m_time_base = timeBase;

    // 1. 设置期望的SDL音频规格
    SDL_AudioSpec wanted_spec;
    SDL_zero(wanted_spec);
    wanted_spec.freq = sampleRate;
    wanted_spec.format = AUDIO_S16SYS;                  // 目标输出为16位有符号音频
    wanted_spec.channels = channels > 2 ? 2 : channels; // 为简易起见，最多支持立体声
    wanted_spec.silence = 0;
    wanted_spec.samples = 1024;                         // 合理的缓冲区大小
    wanted_spec.callback = nullptr;                     // 使用Push模式 (SDL_QueueAudio)

    // 2. 打开音频设备
    m_audio_device_id = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &m_actual_spec, 0);
    if (m_audio_device_id == 0) {
        std::cerr << "SDLAudioRenderer: Failed to open audio device: " << SDL_GetError() << std::endl;
        return false;
    }
    std::cout << "SDLAudioRenderer: Audio device opened with ID " << m_audio_device_id << std::endl;
    std::cout << "SDLAudioRenderer: Freq: " << m_actual_spec.freq << " Format: " << m_actual_spec.format
        << " Channels: " << (int)m_actual_spec.channels << std::endl;

    m_target_sample_fmt = AV_SAMPLE_FMT_S16;
    m_target_channels = m_actual_spec.channels;

    // 3. 检查是否需要重采样
    if (decoderSampleFormat != m_target_sample_fmt || sampleRate != m_actual_spec.freq || channels != m_target_channels) {
        std::cout << "SDLAudioRenderer: Audio resampling is required." << std::endl;
        m_swr_context = swr_alloc();
        if (!m_swr_context) {
            std::cerr << "SDLAudioRenderer: Could not allocate resampler context." << std::endl;
            close();
            return false;
        }

        AVChannelLayout in_ch_layout, out_ch_layout;
        av_channel_layout_default(&in_ch_layout, channels);
        av_channel_layout_default(&out_ch_layout, m_target_channels);

        av_opt_set_chlayout(m_swr_context, "in_chlayout", &in_ch_layout, 0);
        av_opt_set_int(m_swr_context, "in_sample_rate", sampleRate, 0);
        av_opt_set_sample_fmt(m_swr_context, "in_sample_fmt", decoderSampleFormat, 0);

        av_opt_set_chlayout(m_swr_context, "out_chlayout", &out_ch_layout, 0);
        av_opt_set_int(m_swr_context, "out_sample_rate", m_actual_spec.freq, 0);
        av_opt_set_sample_fmt(m_swr_context, "out_sample_fmt", m_target_sample_fmt, 0);

        if (swr_init(m_swr_context) < 0) {
            std::cerr << "SDLAudioRenderer: Failed to initialize the resampling context." << std::endl;
            close();
            return false;
        }
        av_channel_layout_uninit(&in_ch_layout);
        av_channel_layout_uninit(&out_ch_layout);
    }

    // 4. 计算并通知时钟管理器音频硬件参数
    m_bytes_per_second = m_actual_spec.freq * m_actual_spec.channels * SDL_AUDIO_BITSIZE(m_actual_spec.format) / 8;
    if (m_clock_manager) {
        m_clock_manager->setAudioHardwareParams(m_audio_device_id, m_bytes_per_second);
    }

    play(); // 初始化后立即开始播放（设备会播放静音，直到有数据送入）

    return true;
}

bool SDLAudioRenderer::renderFrame(AVFrame* frame, const std::atomic<bool>& quit) {
    if (!frame || !m_clock_manager || m_audio_device_id == 0) {
        return false;
    }

    uint8_t* audio_data = nullptr;
    int data_size = 0;

    if (m_swr_context) { // 需要重采样
        const int out_samples = swr_get_out_samples(m_swr_context, frame->nb_samples);
        const int out_buffer_size = av_samples_get_buffer_size(NULL, m_target_channels, out_samples, m_target_sample_fmt, 1);
        if (out_buffer_size < 0) {
            std::cerr << "SDLAudioRenderer: av_samples_get_buffer_size() failed" << std::endl;
            return false;
        }

        if (m_resampled_buffer_size < out_buffer_size) {
            av_freep(&m_resampled_buffer);
            m_resampled_buffer = (uint8_t*)av_malloc(out_buffer_size);
            if (!m_resampled_buffer) {
                std::cerr << "SDLAudioRenderer: av_malloc for resample buffer failed" << std::endl;
                return false;
            }
            m_resampled_buffer_size = out_buffer_size;
        }

        uint8_t* out_data[1] = { m_resampled_buffer };
        int converted_samples = swr_convert(m_swr_context, out_data, out_samples,
            (const uint8_t**)frame->data, frame->nb_samples);
        if (converted_samples < 0) {
            std::cerr << "SDLAudioRenderer: Error while converting audio." << std::endl;
            return false;
        }

        audio_data = m_resampled_buffer;
        data_size = converted_samples * m_target_channels * av_get_bytes_per_sample(m_target_sample_fmt);
    }
    else { // 不需要重采样，直接使用原始数据
        audio_data = frame->data[0];
        data_size = av_samples_get_buffer_size(nullptr, frame->ch_layout.nb_channels, frame->nb_samples,
            (AVSampleFormat)frame->format, 1);
    }

    // 关键：在推送数据到SDL之前，用当前音频帧的 PTS 更新音频时钟
    double pts = (frame->pts == AV_NOPTS_VALUE) ? 0.0 : frame->pts * av_q2d(m_time_base);
    if (pts != 0.0) {
        m_clock_manager->setAudioClock(pts);
    }

    // 流量控制：如果SDL队列中的数据过多（例如超过1.5秒），则稍作等待
    // 这可以防止内存过度消耗，并能更快地响应跳转seek操作
    const Uint32 max_queued_size = m_bytes_per_second * 1.5;
    while (SDL_GetQueuedAudioSize(m_audio_device_id) > max_queued_size) {
        // 在等待时检查退出标志
        if (quit) {
            std::cout << "SDLAudioRenderer: Quit requested during audio queue wait." << std::endl;
            return false; // 中断并返回，以允许线程退出
        }
        SDL_Delay(10);
    }

    // 将最终的PCM数据推送到SDL的播放队列
    if (SDL_QueueAudio(m_audio_device_id, audio_data, data_size) < 0) {
        std::cerr << "SDLAudioRenderer: Failed to queue audio: " << SDL_GetError() << std::endl;
        return false;
    }

    return true;
}

void SDLAudioRenderer::play() {
    if (m_audio_device_id != 0) {
        SDL_PauseAudioDevice(m_audio_device_id, 0);
    }
}

void SDLAudioRenderer::pause() {
    if (m_audio_device_id != 0) {
        SDL_PauseAudioDevice(m_audio_device_id, 1);
    }
}

void SDLAudioRenderer::flushBuffers() {
    if (m_audio_device_id != 0) {
        SDL_ClearQueuedAudio(m_audio_device_id);
    }
}

void SDLAudioRenderer::close() {
    if (m_audio_device_id != 0) {
        SDL_PauseAudioDevice(m_audio_device_id, 1);
        SDL_CloseAudioDevice(m_audio_device_id);
        m_audio_device_id = 0;
        std::cout << "SDLAudioRenderer: Audio device closed." << std::endl;
    }
    if (m_swr_context) {
        swr_free(&m_swr_context);
    }
    if (m_resampled_buffer) {
        av_freep(&m_resampled_buffer);
        m_resampled_buffer_size = 0;
    }
}