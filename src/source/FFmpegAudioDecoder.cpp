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

#include "../include/FFmpegAudioDecoder.h"
#include <stdexcept>

using namespace std;

FFmpegAudioDecoder::~FFmpegAudioDecoder() { 
    close();
}

void FFmpegAudioDecoder::close() {
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
        m_codec = nullptr;
        m_clockManager = nullptr;
        cout << "FFmpegAudioDecoder: Closed and resources released." << endl;
    }
}

bool FFmpegAudioDecoder::init(AVCodecParameters* codecParams, AVRational timeBase, IClockManager* clockManager) {
    if (!codecParams) {
        cerr << "FFmpegAudioDecoder Error: Codec parameters are null." << endl;
        return false;
    }

    // 1. 查找解码器
    m_codec = avcodec_find_decoder(codecParams->codec_id);
    if (!m_codec) {
        cerr << "FFmpegAudioDecoder Error: No decoder found for codec ID " << codecParams->codec_id << endl;
        return false;
    }

    // 2. 分配解码器上下文
    m_codecContext = avcodec_alloc_context3(m_codec);
    if (!m_codecContext) {
        cerr << "FFmpegAudioDecoder Error: Failed to allocate codec context." << endl;
        return false;
    }

    // 3. 将编解码器参数复制到上下文中
    if (avcodec_parameters_to_context(m_codecContext, codecParams) < 0) {
        cerr << "FFmpegAudioDecoder Error: Failed to copy codec parameters to context." << endl;
        close();
        return false;
    }

    // 4. 打开解码器
    if (avcodec_open2(m_codecContext, m_codec, nullptr) < 0) {
        cerr << "FFmpegAudioDecoder Error: Failed to open codec." << endl;
        close();
        return false;
    }

    // 从参数中存储时间基
    m_timeBase = timeBase;
    // 安全检查，如果传入的时间基无效，则基于采样率设置一个默认值。
    if (m_timeBase.num == 0) {
        m_timeBase = { 1, m_codecContext->sample_rate };
        cout << "FFmpegAudioDecoder Warning: Invalid time base received, defaulting to 1/" << m_codecContext->sample_rate << endl;
    }

    m_clockManager = clockManager;

    cout << "FFmpegAudioDecoder: Initialized successfully for codec " << m_codec->name << "." << endl;
    cout << "  Sample Rate: " << getSampleRate() << " Hz" << endl;
    cout << "  Channels: " << getChannels() << endl;
    cout << "  Sample Format: " << av_get_sample_fmt_name(getSampleFormat()) << endl;
    cout << "  Time Base: " << m_timeBase.num << "/" << m_timeBase.den << endl;

    return true;
}

int FFmpegAudioDecoder::decode(AVPacket* packet, AVFrame** frame) {
    if (!m_codecContext) {
        return AVERROR(EINVAL);
    }

    // 如果用户提供的AVFrame指针为空，则为其分配内存
    if (!*frame) {
        *frame = av_frame_alloc();
        if (!*frame) {
            cerr << "FFmpegAudioDecoder::decode: Could not allocate AVFrame." << endl;
            return AVERROR(ENOMEM);
        }
    }
    else {
        // 确保传入的AVFrame在使用前是干净的
        av_frame_unref(*frame);
    }

    // 1. 发送数据包到解码器
    int ret = avcodec_send_packet(m_codecContext, packet);
    if (ret < 0) {
        if (ret != AVERROR(EAGAIN)) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(errbuf, sizeof(errbuf), ret);
            cerr << "FFmpegAudioDecoder Error: avcodec_send_packet failed: " << errbuf << endl;
        }
        return ret;
    }

    // 2. 从解码器接收数据帧
    ret = avcodec_receive_frame(m_codecContext, *frame);
    // 返回值 AVERROR(EAGAIN) 表示需要发送更多输入数据包。
    // 返回值 AVERROR_EOF 表示解码器已完全刷新。
    // 返回值 0 表示成功解码一帧。
    return ret;
}

void FFmpegAudioDecoder::flush() {
    if (m_codecContext) {
        avcodec_flush_buffers(m_codecContext);
    }
}

// --- Getter 方法 ---

int FFmpegAudioDecoder::getSampleRate() const {
    return m_codecContext ? m_codecContext->sample_rate : 0;
}

int FFmpegAudioDecoder::getChannels() const {
    return m_codecContext ? m_codecContext->ch_layout.nb_channels : 0;
}

enum AVSampleFormat FFmpegAudioDecoder::getSampleFormat() const {
    return m_codecContext ? m_codecContext->sample_fmt : AV_SAMPLE_FMT_NONE;
}

struct AVRational FFmpegAudioDecoder::getTimeBase() const {
    return m_timeBase;
}

int FFmpegAudioDecoder::getBytesPerSampleFrame() const {
    if (!m_codecContext) return 0;
    return av_get_bytes_per_sample(m_codecContext->sample_fmt) * m_codecContext->ch_layout.nb_channels;
}
