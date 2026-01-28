/*
 * SDLplayerCore - An audio and video player.
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

    // 播放器状态
    // 0:IDLE, 1:BUFFERING, 2:PLAYING, 3:PAUSED, 4:STOPPED
    std::atomic<int> current_state{ 0 };
};
