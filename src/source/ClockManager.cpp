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

#include "../include/ClockManager.h"
#include <iostream>
#include <cassert>

ClockManager::ClockManager() :
    m_video_clock_time(0.0),
    m_audio_clock_time(0.0),
    m_start_time(0),
    m_paused_at(0),
    m_paused(false),
    m_master_clock_type(MasterClockType::AUDIO) {}

void ClockManager::init(bool has_audio, bool has_video) {
    reset();

    std::lock_guard<std::mutex> lock(m_mutex);

    // 存储流信息
    m_has_audio_stream = has_audio;
    m_has_video_stream = has_video;

    // 根据可用流智能选择最佳主时钟
    // 策略：有音频用音频，没音频但有视频用视频，都没有用外部时钟
    if (m_has_audio_stream) {
        m_master_clock_type = MasterClockType::AUDIO;
        std::cout << "ClockManager: Initializing with AUDIO master clock." << std::endl;
    }
    else if (m_has_video_stream) {
        m_master_clock_type = MasterClockType::VIDEO;
        std::cout << "ClockManager: Initializing with VIDEO master clock (no audio stream)." << std::endl;
    }
    else {
        m_master_clock_type = MasterClockType::EXTERNAL;
        std::cout << "ClockManager: Initializing with EXTERNAL master clock (no A/V streams)." << std::endl;
    }
}

void ClockManager::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_video_clock_time = 0.0;
    m_audio_clock_time = 0.0;

    // 重置外部时钟基准
    m_start_time = SDL_GetTicks64(); // 获取自SDL初始化以来的毫秒数
    
    m_paused_at = 0;
    m_paused = false;

    // 恢复到默认主时钟
    m_master_clock_type = MasterClockType::AUDIO;

    // 重置音频硬件参数相关的状态
    m_has_audio_stream = false;
    m_audio_device_id = 0;
    m_audio_bytes_per_second = 0;

    m_has_video_stream = false;
    
    std::cout << "ClockManager reset." << std::endl;
}

double ClockManager::getExternalClockTime() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return getExternalClockTime_nolock();
}

double ClockManager::getExternalClockTime_nolock() {
    Uint64 now;
    if (m_paused) {
        now = m_paused_at;
    }
    else {
        now = SDL_GetTicks64();
    }
    return (double)(now - m_start_time) / 1000.0;
}

void ClockManager::setMasterClock(MasterClockType type) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_master_clock_type = type;
}

// 主时钟的选择逻辑
double ClockManager::getMasterClockTime() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_master_clock_type == MasterClockType::VIDEO) {
        if (m_has_video_stream) {
            return getVideoClockTime_nolock();
        }
        // 如果主时钟是视频但视频流不存在（异常情况），则回退
        return getExternalClockTime_nolock();
    }
    else if (m_master_clock_type == MasterClockType::AUDIO) {
        if (m_has_audio_stream) {
            return getAudioClockTime_nolock();
        }
        // 如果主时钟是音频但音频流不存在，则尝试回退到视频
        if (m_has_video_stream) {
            return getVideoClockTime_nolock();
        }
        // 如果视频也没有，最终回退到外部
        return getExternalClockTime_nolock();
    }
    else { // EXTERNAL
        return getExternalClockTime_nolock();
    }
}

void ClockManager::setVideoClock(double pts) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_video_clock_time = pts;
}

double ClockManager::getVideoClockTime() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return getVideoClockTime_nolock();
}

double ClockManager::getVideoClockTime_nolock() {
    return m_video_clock_time;
}

void ClockManager::setAudioHardwareParams(SDL_AudioDeviceID deviceId, int bytesPerSecond) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 断言，确保在调用此函数时，确实有音频流
    assert(m_has_audio_stream && "setAudioHardwareParams called but no audio stream was reported on init");
    m_audio_device_id = deviceId;
    m_audio_bytes_per_second = bytesPerSecond;
}

// 此方法仅存储最新帧的PTS
void ClockManager::setAudioClock(double pts) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_audio_clock_time = pts;
}

// 实现精确的音频时钟计算
double ClockManager::getAudioClockTime() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return getAudioClockTime_nolock();
}

double ClockManager::getAudioClockTime_nolock() {
    if (!m_has_audio_stream || m_audio_device_id == 0 || m_audio_bytes_per_second == 0) {
        return 0.0;
    }

    // 基础时间是最后推送的音频帧的PTS
    double pts = m_audio_clock_time;
    // 获取SDL音频队列中还未播放的数据字节数
    Uint32 buffered_bytes = SDL_GetQueuedAudioSize(m_audio_device_id);
    // 计算这些未播放数据的时长（秒）
    double buffered_duration_sec = (double)buffered_bytes / (double)m_audio_bytes_per_second;
    // 当前的音频播放时间 = 最新PTS - 未播放时长
    pts -= buffered_duration_sec;
    return pts;
}

void ClockManager::pause() {
    if (!m_paused) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_paused_at = SDL_GetTicks64();
        m_paused = true;

        // 暂停SDL音频设备
        if (m_audio_device_id != 0) {
            SDL_PauseAudioDevice(m_audio_device_id, 1);
        }
        std::cout << "Clock paused." << std::endl;
    }
}

void ClockManager::resume() {
    if (m_paused) {
        std::lock_guard<std::mutex> lock(m_mutex);
        // 恢复时，将暂停期间的时间加到起始时间上，以抵消暂停的影响
        Uint64 paused_duration = SDL_GetTicks64() - m_paused_at;
        m_start_time += paused_duration;
        m_paused = false;

        // 恢复SDL音频设备的播放状态
        if (m_audio_device_id != 0) {
            SDL_PauseAudioDevice(m_audio_device_id, 0);
        }
        std::cout << "Clock resumed." << std::endl;
    }
}

bool ClockManager::isPaused() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_paused;
}
