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
    std::lock_guard<std::mutex> lock(m_mutex);
    m_master_hint = hint;
    reset();
}

void ClockManager::reset() {
    // 假定锁由调用者（init）持有或在内部完成
    m_video_clock_time = 0.0;
    m_audio_clock_time = 0.0;
    m_paused = false;
    m_start_time = SDL_GetTicks64(); // 获取自SDL初始化以来的毫秒数
    m_paused_at = 0;
    std::cout << "ClockManager initialized/reset." << std::endl;
}

void ClockManager::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_video_clock_time = 0.0;
    m_audio_clock_time = 0.0;
    m_start_time = 0;
    m_paused_at = 0;
    m_paused = false;
    m_master_hint = InitialMasterHint::PREFER_AUDIO; // 重置 hint
    std::cout << "ClockManager closed." << std::endl;
}

double ClockManager::getExternalClockTime() {
    if (m_paused) {
        return (double)(m_paused_at - m_start_time) / 1000.0;
    }
    else {
        return (double)(SDL_GetTicks64() - m_start_time) / 1000.0;
    }
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

void ClockManager::setVideoClock(double pts, double duration) {
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
    return m_paused;
}
