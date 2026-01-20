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

#include "IVideoRenderer.h"
#include "OSDLayer.h"
#include "PlayerDebugStats.h"

#include <string>
#include <iostream>
#include <mutex>

#include "SDL2/SDL.h"

extern "C" {
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

class SDLVideoRenderer : public IVideoRenderer {
private:
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture* m_texture = nullptr;
    SwsContext* m_sws_context = nullptr;
    AVFrame* m_yuv_frame = nullptr;

    IClockManager* m_clock_manager = nullptr;
    AVRational m_time_base;         // 视频流的时间基，用于PTS计算

    double m_frame_last_pts = 0.0;      // 上一帧的PTS
    double m_frame_last_duration = 0.04; // 帧持续时间的估计值 (默认25fps)
    int m_video_width = 0;      // 视频原始宽度
    int m_video_height = 0;     // 视频原始高度
    int m_window_width = 0;     // 当前窗口宽度
    int m_window_height = 0;    // 当前窗口高度

    // 用于暂存配置的成员变量
    std::string m_window_title;
    enum AVPixelFormat m_decoder_pixel_format;
    bool m_is_audio_only = false;   // 标记是否为纯音频模式
    bool m_is_live_stream = false;  // 标记是否为直播流

    std::mutex m_mutex;                         // 用于保护对SDL资源的访问
    AVFrame* m_last_rendered_frame = nullptr;   // 保存最后一帧的副本，用于刷新和恢复
    
    bool m_first_frame_after_reset = true;      // 用于处理 Reset 后第一帧的特殊逻辑

    // 调试信息相关成员
    std::unique_ptr<OSDLayer> m_osd_layer;
    std::shared_ptr<PlayerDebugStats> m_debug_stats;

    // 计算保持宽高比的显示矩形
    SDL_Rect calculateDisplayRect(int windowWidth, int windowHeight) const;
    
    // 绘制OSD调试层
    void renderOSD();

public:
    SDLVideoRenderer() = default;
    virtual ~SDLVideoRenderer();

    bool init(const char* windowTitle, int width, int height,
              enum AVPixelFormat decoderPixelFormat, IClockManager* clockManager) override;

    /**
     * @brief 为渲染器设置关键的同步参数。
     * @param time_base 从解封装器获取的视频流时间基
     * @param frame_rate 视频的平均帧率，用于估算帧持续时间
     */
    void setSyncParameters(AVRational time_base, double frame_rate);

    void setDebugStats(std::shared_ptr<PlayerDebugStats> stats) override;

    void setStreamType(bool isLive) override;

    // 渲染逻辑相关方法
    double calculateSyncDelay(AVFrame* frame) override;
    bool prepareFrameForDisplay(AVFrame* frame) override;
    void displayFrame() override; // 在主线程中调用

    void close() override;
    void refresh() override;

    bool onWindowResize(int newWidth, int newHeight) override;
    void getWindowSize(int& width, int& height) const override;

    void flush() override;
};
