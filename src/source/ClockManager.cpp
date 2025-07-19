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

ClockManager::ClockManager() :
    m_video_clock_time(0.0),
    m_audio_clock_time(0.0),
    m_start_time(0),
    m_paused_at(0),
    m_paused(false),
    m_master_hint(InitialMasterHint::PREFER_AUDIO) {}

void ClockManager::init(InitialMasterHint hint) {
    reset();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_master_hint = hint;
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
    m_master_hint = InitialMasterHint::PREFER_AUDIO;

    // 重置音频硬件参数相关的状态
    m_has_audio_stream = false;
    m_audio_device_id = 0;
    m_audio_bytes_per_second = 0;
    
    std::cout << "ClockManager reset." << std::endl;
}

double ClockManager::getExternalClockTime() {
    std::lock_guard<std::mutex> lock(m_mutex);

    Uint64 now;
    if (m_paused) {
        now = m_paused_at;
    }
    else {
        now = SDL_GetTicks64();
    }
    return (double)(now - m_start_time) / 1000.0;
}

// 实现主时钟的选择逻辑
double ClockManager::getMasterClockTime() {
    // getAudioClockTime 和 getExternalClockTime 内部有锁，这里不需要外层锁
    if (m_master_hint == InitialMasterHint::PREFER_AUDIO && m_has_audio_stream) {
        return getAudioClockTime();
    }

    // 默认或无音频时，回退到外部时钟（系统时间）
    return getExternalClockTime();
}

void ClockManager::setVideoClock(double pts) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_video_clock_time = pts;
}

double ClockManager::getVideoClockTime() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_video_clock_time;
}

void ClockManager::setAudioHardwareParams(SDL_AudioDeviceID deviceId, int bytesPerSecond, bool hasAudioStream) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_audio_device_id = deviceId;
    m_audio_bytes_per_second = bytesPerSecond;
    m_has_audio_stream = hasAudioStream;
}

// 此方法仅存储最新帧的PTS
void ClockManager::setAudioClock(double pts) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_audio_clock_time = pts;
}

// 实现精确的音频时钟计算
double ClockManager::getAudioClockTime() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_has_audio_stream || m_audio_device_id == 0 || m_audio_bytes_per_second == 0) {
        return 0.0; // 如果没有音频或未初始化，返回0
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
