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
#include <string>
#include <iostream>
#include <mutex>

#include "SDL2/SDL.h"

extern "C" {
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

/**
 * @class SDLVideoRenderer
 * @brief IVideoRenderer的一个实现，使用SDL2进行视频渲染。
 *
 * 这个类封装了SDL窗口、渲染器、纹理的创建和管理。
 * 核心功能在renderFrame()中，它包含了与IClockManager协作的视频同步逻辑。
 */
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

    // 计算保持宽高比的显示矩形
    SDL_Rect calculateDisplayRect(int windowWidth, int windowHeight) const;

    std::mutex m_mutex; // 用于保护对SDL资源的访问

    // 保存最后一帧的副本，用于刷新和恢复
    AVFrame* m_last_rendered_frame = nullptr;
    bool m_texture_lost = false; // 标记纹理内容是否可能已丢失

    bool m_is_audio_only = false;   // 标记是否为纯音频模式

public:
    SDLVideoRenderer() = default;
    virtual ~SDLVideoRenderer();

    // 接口要求的初始化方法
    bool init(const char* windowTitle, int width, int height,
        enum AVPixelFormat decoderPixelFormat, IClockManager* clockManager) override;

    /**
     * @brief 为渲染器设置关键的同步参数。
     * 必须在调用 renderFrame() 之前调用。
     * @param time_base 从FFmpeg demuxer获取的视频流时间基。
     * @param frame_rate 视频的平均帧率，用于估算帧持续时间。
     */
    void setSyncParameters(AVRational time_base, double frame_rate);

    bool renderFrame(AVFrame* frame) override;
    void close() override;
    void refresh() override;

    bool onWindowResize(int newWidth, int newHeight) override;
    void getWindowSize(int& width, int& height) const override;

    /**
    * @brief 获取窗口指针的方法，以供外部检查窗口状态
    */
    SDL_Window* getWindow() const { return m_window; }

    /**
    * @brief 为纯音频播放模式设计的初始化方法，初始化一个窗口和渲染器，但不包含视频相关资源
    */
    bool initForAudioOnly(const char* windowTitle, int width, int height, IClockManager* clockManager);
};
