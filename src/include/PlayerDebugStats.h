#pragma once

#include <atomic>
#include <string>
#include <chrono>

// 简单 FPS 计数器工具
class FPSCounter {
private:
    std::atomic<int> m_frame_count{ 0 };
    std::atomic<int> m_fps{ 0 };
    std::chrono::steady_clock::time_point m_last_time;

public:
    FPSCounter() {
        m_last_time = std::chrono::steady_clock::now();
    }

    // 在每一帧/每次解码时调用
    void tick() {
        m_frame_count++;
        auto now = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_time).count();
        if (diff >= 1000) {
            m_fps.store(m_frame_count.load());
            m_frame_count.store(0);
            m_last_time = now;
        }
    }

    int getFPS() const {
        return m_fps.load();
    }
};

// 全局调试状态结构体
struct PlayerDebugStats {
    // V-Q (Video Queue) Info
    std::atomic<int> vq_size{ 0 };
    std::atomic<long long> vq_duration_ms{ 0 };

    // A-V (Audio Video) Sync
    std::atomic<double> av_diff_ms{ 0.0 };
    std::atomic<double> video_current_pts{ 0.0 };
    std::atomic<double> master_clock_val{ 0.0 };

    // Clock Source (0: Audio, 1: External)
    std::atomic<int> clock_source_type{ 0 }; 

    // FPS
    FPSCounter decode_fps;
    FPSCounter render_fps;
};
