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

    // �洢����Ϣ
    m_has_audio_stream = has_audio;
    m_has_video_stream = has_video;

    // ���ݿ���������ѡ�������ʱ��
    // ���ԣ�����Ƶ����Ƶ��û��Ƶ������Ƶ����Ƶ����û�����ⲿʱ��
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

    // �����ⲿʱ�ӻ�׼
    m_start_time = SDL_GetTicks64(); // ��ȡ��SDL��ʼ�������ĺ�����
    
    m_paused_at = 0;
    m_paused = false;

    // �ָ���Ĭ����ʱ��
    m_master_clock_type = MasterClockType::AUDIO;

    // ������ƵӲ��������ص�״̬
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

// ��ʱ�ӵ�ѡ���߼�
double ClockManager::getMasterClockTime() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_master_clock_type == MasterClockType::VIDEO) {
        if (m_has_video_stream) {
            return getVideoClockTime_nolock();
        }
        // �����ʱ������Ƶ����Ƶ�������ڣ��쳣������������
        return getExternalClockTime_nolock();
    }
    else if (m_master_clock_type == MasterClockType::AUDIO) {
        if (m_has_audio_stream) {
            return getAudioClockTime_nolock();
        }
        // �����ʱ������Ƶ����Ƶ�������ڣ����Ի��˵���Ƶ
        if (m_has_video_stream) {
            return getVideoClockTime_nolock();
        }
        // �����ƵҲû�У����ջ��˵��ⲿ
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
    // ���ԣ�ȷ���ڵ��ô˺���ʱ��ȷʵ����Ƶ��
    assert(m_has_audio_stream && "setAudioHardwareParams called but no audio stream was reported on init");
    m_audio_device_id = deviceId;
    m_audio_bytes_per_second = bytesPerSecond;
}

// �˷������洢����֡��PTS
void ClockManager::setAudioClock(double pts) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_audio_clock_time = pts;
}

// ʵ�־�ȷ����Ƶʱ�Ӽ���
double ClockManager::getAudioClockTime() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return getAudioClockTime_nolock();
}

double ClockManager::getAudioClockTime_nolock() {
    if (!m_has_audio_stream || m_audio_device_id == 0 || m_audio_bytes_per_second == 0) {
        return 0.0;
    }

    // ����ʱ����������͵���Ƶ֡��PTS
    double pts = m_audio_clock_time;
    // ��ȡSDL��Ƶ�����л�δ���ŵ������ֽ���
    Uint32 buffered_bytes = SDL_GetQueuedAudioSize(m_audio_device_id);
    // ������Щδ�������ݵ�ʱ�����룩
    double buffered_duration_sec = (double)buffered_bytes / (double)m_audio_bytes_per_second;
    // ��ǰ����Ƶ����ʱ�� = ����PTS - δ����ʱ��
    pts -= buffered_duration_sec;
    return pts;
}

void ClockManager::pause() {
    if (!m_paused) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_paused_at = SDL_GetTicks64();
        m_paused = true;

        // ��ͣSDL��Ƶ�豸
        if (m_audio_device_id != 0) {
            SDL_PauseAudioDevice(m_audio_device_id, 1);
        }
        std::cout << "Clock paused." << std::endl;
    }
}

void ClockManager::resume() {
    if (m_paused) {
        std::lock_guard<std::mutex> lock(m_mutex);
        // �ָ�ʱ������ͣ�ڼ��ʱ��ӵ���ʼʱ���ϣ��Ե�����ͣ��Ӱ��
        Uint64 paused_duration = SDL_GetTicks64() - m_paused_at;
        m_start_time += paused_duration;
        m_paused = false;

        // �ָ�SDL��Ƶ�豸�Ĳ���״̬
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
