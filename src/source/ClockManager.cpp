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
    // �ٶ����ɵ����ߣ�init�����л����ڲ����
    m_video_clock_time = 0.0;
    m_audio_clock_time = 0.0;
    m_paused = false;
    m_start_time = SDL_GetTicks64(); // ��ȡ��SDL��ʼ�������ĺ�����
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
    m_master_hint = InitialMasterHint::PREFER_AUDIO; // ���� hint
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

// ʵ����ʱ�ӵ�ѡ���߼�
double ClockManager::getMasterClockTime() {
    // getAudioClockTime �� getExternalClockTime �ڲ����������ﲻ��Ҫ�����
    if (m_master_hint == InitialMasterHint::PREFER_AUDIO && m_has_audio_stream) {
        return getAudioClockTime();
    }

    // Ĭ�ϻ�����Ƶʱ�����˵��ⲿʱ�ӣ�ϵͳʱ�䣩
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

// �˷������洢����֡��PTS
void ClockManager::setAudioClock(double pts) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_audio_clock_time = pts;
}

// ʵ�־�ȷ����Ƶʱ�Ӽ���
double ClockManager::getAudioClockTime() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_has_audio_stream || m_audio_device_id == 0 || m_audio_bytes_per_second == 0) {
        return 0.0; // ���û����Ƶ��δ��ʼ��������0
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
    return m_paused;
}
