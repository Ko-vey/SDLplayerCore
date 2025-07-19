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
#include "SDL2/SDL_timer.h" // SDL_GetTicks64
#include "SDL2/SDL_audio.h" // SDL_AudioDeviceID

class ClockManager : public IClockManager {
public:
    ClockManager();
    virtual ~ClockManager() = default;

    // 实现 IClockManager 接口
    void init(InitialMasterHint hint) override;
    void reset() override;

    double getMasterClockTime() override;

    void setAudioClock(double pts) override;
    double getAudioClockTime() override;
    void setAudioHardwareParams(SDL_AudioDeviceID deviceId, int bytesPerSecond, bool hasAudioStream) override;

    void setVideoClock(double pts, double duration) override;
    double getVideoClockTime() override;

    double getExternalClockTime() override;

    void pause() override;
    void resume() override;
    bool isPaused() const override;

private:
    mutable std::mutex m_mutex;

    double m_video_clock_time;
    double m_audio_clock_time; // 最后推送到队列的音频块的PTS

    Uint64 m_start_time; // 使用SDL的高精度计时器
    Uint64 m_paused_at;

    bool m_paused;
    InitialMasterHint m_master_hint;

    SDL_AudioDeviceID m_audio_device_id;
    int m_audio_bytes_per_second;
    bool m_has_audio_stream;
};
