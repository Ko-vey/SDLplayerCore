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

#include "IClockManager.h"
#include <mutex>
#include <cmath> // std::isnan

#include "SDL2/SDL_timer.h" // SDL_GetTicks64
#include "SDL2/SDL_audio.h" // SDL_AudioDeviceID

class ClockManager : public IClockManager {
public:
    ClockManager();
    virtual ~ClockManager() = default;

    // IClockManager 接口实现
    void init(bool has_audio, bool has_video) override;
    void reset() override;

    void setMasterClock(MasterClockType type) override;
    double getMasterClockTime() override;

    void setAudioClock(double pts) override;
    double getAudioClockTime() override;

    void setAudioHardwareParams(SDL_AudioDeviceID deviceId, int bytesPerSecond) override;

    void setVideoClock(double pts) override;
    double getVideoClockTime() override;

    double getExternalClockTime() override;

    void setClockToUnknown() override;
    bool isClockUnknown() override;

    void pause() override;
    void resume() override;
    bool isPaused() const override;
    void syncToPts(double pts) override;

private:
    // 不加锁的内部Getters
    double getAudioClockTime_nolock();
    double getVideoClockTime_nolock();
    double getExternalClockTime_nolock();

private:
    mutable std::mutex m_mutex;

    double m_video_clock_time;
    double m_audio_clock_time;

    Uint64 m_start_time;
    Uint64 m_paused_at;

    bool m_paused;
    MasterClockType m_master_clock_type;

    SDL_AudioDeviceID m_audio_device_id;
    int m_audio_bytes_per_second;
    bool m_has_audio_stream;
    bool m_has_video_stream;
};
