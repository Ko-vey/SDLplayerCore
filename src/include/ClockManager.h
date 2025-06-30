#pragma once

#include "IClockManager.h"
#include <mutex>
#include <atomic>
#include "SDL2/SDL_timer.h" // SDL_GetTicks64
#include "SDL2/SDL_audio.h" // SDL_AudioDeviceID

class ClockManager : public IClockManager {
public:
    ClockManager();
    virtual ~ClockManager() = default;

    // 实现 IClockManager 接口
    void init(InitialMasterHint hint) override;
    void reset() override;
    void close() override;

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
    std::mutex m_mutex;

    double m_video_clock_time;
    double m_audio_clock_time; // 最后推送到队列的音频块的PTS

    Uint64 m_start_time; // 使用SDL的高精度计时器
    Uint64 m_paused_at;

    std::atomic<bool> m_paused;
    InitialMasterHint m_master_hint;

    SDL_AudioDeviceID m_audio_device_id{ 0 };
    int m_audio_bytes_per_second{ 0 };
    std::atomic<bool> m_has_audio_stream{ false };
};
