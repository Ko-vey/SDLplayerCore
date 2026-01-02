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
    m_paused(true), // 构造时默认暂停，防止未初始化时钟乱跑
    m_master_clock_type(MasterClockType::AUDIO),
    m_audio_device_id(0),
    m_audio_bytes_per_second(0),
    m_has_audio_stream(false),
    m_has_video_stream(false) 
{
}

void ClockManager::init(bool has_audio, bool has_video) {
    // init 内部调用 reset，会将状态置为 paused
    reset();

    std::lock_guard<std::mutex> lock(m_mutex);

    // 存储流信息
    m_has_audio_stream = has_audio;
    m_has_video_stream = has_video;

    if (m_has_audio_stream) {
        m_master_clock_type = MasterClockType::AUDIO;
        std::cout << "ClockManager: Init with AUDIO master clock." << std::endl;
    }
    else {
        m_master_clock_type = MasterClockType::EXTERNAL;
        std::cout << "ClockManager: Init with EXTERNAL master clock." << std::endl;
    }
}

void ClockManager::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 重置时间值
    m_video_clock_time = 0.0;
    m_audio_clock_time = 0.0;

    // 重置状态为暂停
    // 避免外部时钟在加载文件时就开始空转计时
    m_paused = true;
    m_start_time = SDL_GetTicks64(); // 获取自SDL初始化以来的毫秒数
    m_paused_at = m_start_time; // 暂停时间点对齐到当前

    // 默认回退到音频主时钟 (如果配置了音频)
    m_master_clock_type = MasterClockType::AUDIO;

    std::cout << "ClockManager reset (paused). Configuration retained." << std::endl;
}

double ClockManager::getExternalClockTime() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return getExternalClockTime_nolock();
}

double ClockManager::getExternalClockTime_nolock() {
    Uint64 now;
    if (m_paused) {
        // 如果处于暂停，时间定格在暂停那一刻
        now = m_paused_at;
    }
    else {
        now = SDL_GetTicks64();
    }
    // 外部时钟 = 当前（或暂停）时刻 - 启动时刻
    return (double)(now - m_start_time) / 1000.0;
}

void ClockManager::setMasterClock(MasterClockType type) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_master_clock_type = type;
}

double ClockManager::getMasterClockTime() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 优先使用音频时钟，但如果配置了音频流却因为设备未就绪等原因无效，则自动回退
    if (m_master_clock_type == MasterClockType::AUDIO && m_has_audio_stream) {
        return getAudioClockTime_nolock();
    }
    return getExternalClockTime_nolock();
}

MasterClockType ClockManager::getMasterClockType() const {
    // lock_guard 自动加锁解锁
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_master_clock_type;
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
    assert(m_has_audio_stream && "setAudioHardwareParams called without audio stream");

    // 参数校验，防止除零
    if (bytesPerSecond <= 0) {
        std::cerr << "ClockManager Error: Invalid bytesPerSecond: " << bytesPerSecond << std::endl;
        return;
    }

    m_audio_device_id = deviceId;
    m_audio_bytes_per_second = bytesPerSecond;
}

void ClockManager::setAudioClock(double pts) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_audio_clock_time = pts;
}

double ClockManager::getAudioClockTime() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return getAudioClockTime_nolock();
}

double ClockManager::getAudioClockTime_nolock() {
    if (!m_has_audio_stream || m_audio_device_id == 0 || m_audio_bytes_per_second <= 0) {
        return 0.0;
    }

    // 基础时间是最后推送到 SDL 的音频帧的结束时间戳 (PTS)
    double pts = m_audio_clock_time;

    // 获取 SDL 内部缓冲区中剩余未播放的字节数
    // 注意：SDL_GetQueuedAudioSize 是线程安全的，但在 lock 保护下调用也没问题
    Uint32 buffered_bytes = SDL_GetQueuedAudioSize(m_audio_device_id);

    // 计算缓冲区的延迟时间
    double buffered_duration_sec = (double)buffered_bytes / (double)m_audio_bytes_per_second;

    // 当前正在播放的声音时间 = 缓冲末尾时间 - 缓冲区长度
    // 公式： \[ T_{play} = PTS_{last\_written} - \frac{Bytes_{buffered}}{Bytes_{per\_second}} \]
    return pts - buffered_duration_sec;
}

void ClockManager::setClockToUnknown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 使用 NAN (Not A Number) 标记未知状态
    m_video_clock_time = std::nan("");
    m_audio_clock_time = std::nan("");
    // 保持 paused 状态，直到 resume 被调用且第一帧到来
    std::cout << "ClockManager set to UNKNOWN status." << std::endl;
}

bool ClockManager::isClockUnknown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 检查当前的主时钟是否为 NAN
    if (m_master_clock_type == MasterClockType::AUDIO) {
        return std::isnan(m_audio_clock_time);
    }
    else {
        return std::isnan(m_video_clock_time);
    }
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
        // 核心逻辑：恢复时，将暂停期间流逝的时间加到 m_start_time 上
        // 这样 (Now - Start) 就会剔除掉暂停的这段时长
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

void ClockManager::syncToPts(double pts) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::cout << "ClockManager: Syncing to PTS: " << pts << "s" << std::endl;

    // 1. 如果是音频主时钟，且音频流存在，直接更新音频时钟值
    if (m_has_audio_stream) {
        m_audio_clock_time = pts;
    }

    // 2. 更新视频时钟值
    m_video_clock_time = pts;

    // 3. 校准外部时钟的基准时间
    // 确保无论当前主时钟是 Audio 还是 External，基准都已经对齐
    Uint64 now = m_paused ? m_paused_at : SDL_GetTicks64();
    m_start_time = now - static_cast<Uint64>(pts * 1000.0);
}
