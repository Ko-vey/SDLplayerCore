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

#include "../include/SDLVideoRenderer.h"
#include <algorithm> // std::max
#include <iostream>

// 如果视频帧比主时钟快，等待。
// 如果视频帧比主时钟慢超过这个阈值（秒），就认为它“太晚”了。
constexpr double AV_SYNC_THRESHOLD_MIN = 0.04;
// 如果视频帧比主时钟慢超过这个阈值，但小于最大值，加速播放（不延迟）。
constexpr double AV_SYNC_THRESHOLD_MAX = 0.1;
// 如果视频帧没有提供 duration，使用这个默认值（对应25fps）
constexpr double DEFAULT_FRAME_DURATION = 0.04;

SDLVideoRenderer::~SDLVideoRenderer() {
    close();
}

bool SDLVideoRenderer::init(const char* windowTitle, int width, int height,
                            enum AVPixelFormat decoderPixelFormat, IClockManager* clockManager) {
    // 不需要锁，因为在主线程中只被调用一次

    // 根据像素格式判断是否为纯音频模式
    if (decoderPixelFormat == AV_PIX_FMT_NONE) {
        m_is_audio_only = true;
    }

    m_window = SDL_CreateWindow(windowTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!m_window) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        return false;
    }

    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) {
        std::cerr << "Could not create accelerated renderer, falling back to software. Error: " << SDL_GetError() << std::endl;
        m_renderer = SDL_CreateRenderer(m_window, -1, 0);
        if (!m_renderer) {
            std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
            return false;
        }
    }

    m_decoder_pixel_format = decoderPixelFormat;
    // 记录视频和窗口的初始尺寸
    m_video_width = width;
    m_video_height = height;
    m_window_width = width;
    m_window_height = height; 

    m_window_title = windowTitle;
    m_clock_manager = clockManager;

    // 如果是纯音频模式，无需创建视频资源，初始化完成，直接返回
    if (m_is_audio_only) {
        std::cout << "SDLVideoRenderer: Initialized in audio-only mode." << std::endl;
        refresh(); // 设置初始背景色
        return true;
    }

    // Texture 尺寸固定为视频原始分辨率
    m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
                                m_video_width, m_video_height);
    if (!m_texture) {
        std::cerr << "Texture could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        return false;
    }

    // SwsContext 只用于色彩转换，不缩放
    m_sws_context = sws_getContext(m_video_width, m_video_height, m_decoder_pixel_format,
                                m_video_width, m_video_height, AV_PIX_FMT_YUV420P,
                                SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws_context) {
        std::cerr << "Could not create SwsContext" << std::endl;
        return false;
    }

    // 为 YUV 数据分配内存
    m_yuv_frame = av_frame_alloc();
    if (!m_yuv_frame) return false;
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, m_video_width, m_video_height, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(m_yuv_frame->data, m_yuv_frame->linesize, buffer, AV_PIX_FMT_YUV420P,
                        m_video_width, m_video_height, 1);

    // 分配用于保存最后一帧的 AVFrame
    m_last_rendered_frame = av_frame_alloc();
    if (!m_last_rendered_frame) {
        std::cerr << "Could not allocate last rendered frame." << std::endl;
        return false;
    }

    std::cout << "SDLVideoRenderer: Initialization succeed."<<std::endl;
    return true;
}

void SDLVideoRenderer::setSyncParameters(AVRational time_base, double frame_rate) {
    m_time_base = time_base;
    if (frame_rate > 0) {
        // 一帧的持续时间（秒）
        m_frame_last_duration = 1.0 / frame_rate;
    }
    else {
        m_frame_last_duration = DEFAULT_FRAME_DURATION; // 默认值
    }
    m_frame_last_pts = 0.0;
}

// 在工作线程中执行
double SDLVideoRenderer::calculateSyncDelay(AVFrame* frame) {
    if (!frame || !m_clock_manager) return 0.0;

    // 计算当前帧的PTS和duration（秒）
    double pts;
    if (frame->pts != AV_NOPTS_VALUE) {
        // 如果没有PTS，就基于上一帧的PTS进行估算
        pts = frame->pts * av_q2d(m_time_base);
    }
    else {
        // 如果PTS未知或为0，基于上一帧进行估算
        pts = m_frame_last_pts + m_frame_last_duration;
    }

    // 计算帧的持续时间 duration（秒）
    double duration = (frame->duration > 0) ? (frame->duration * av_q2d(m_time_base)) : m_frame_last_duration;

    // 更新上一帧的信息，用于下一次循环的估算
    m_frame_last_pts = pts;
    m_frame_last_duration = duration;

    // 更新视频时钟
    m_clock_manager->setVideoClock(pts);
    // 计算视频时钟与主时钟的差值（即延时）
    double delay = pts - m_clock_manager->getMasterClockTime();

    // 根据延时进行同步决策
    const double AV_SYNC_THRESHOLD_MIN = 0.04; // 同步阈值下限 (40ms)
    const double AV_SYNC_THRESHOLD_MAX = 0.1;  // 同步阈值上限 (100ms)
    const double AV_NOSYNC_THRESHOLD = 10.0;   // 非同步（重置）阈值 (10s)

    // 检查延时是否过大，过大则认为时钟不同步，重置延时
    if (delay > AV_NOSYNC_THRESHOLD || delay < -AV_NOSYNC_THRESHOLD) {
        // 时钟差距过大，可能出错了，重置时钟
        std::cout << "VideoRenderer: Clock difference is too large (" << delay << "s), resetting delay." << std::endl;
        delay = 0;
    }

    if (delay < -AV_SYNC_THRESHOLD_MAX) { // 视频严重落后，不等待，立即显示
        return 0.0;
    }

    if (delay > AV_SYNC_THRESHOLD_MAX) { // 视频超前太多，截断等待时间
        return AV_SYNC_THRESHOLD_MAX;
    }

    return delay;
}

// 在工作线程中执行
bool SDLVideoRenderer::prepareFrameForDisplay(AVFrame* frame) {
    if (m_is_audio_only || !frame) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_sws_context || !m_yuv_frame) return false;

    // 保存最后一帧的引用，用于窗口尺寸调整后的重绘
    av_frame_unref(m_last_rendered_frame); // 先释放旧的引用
    if (av_frame_ref(m_last_rendered_frame, frame) < 0) {
        std::cerr << "SDLVideoRenderer: Failed to reference last frame." << std::endl;
        // 非致命错误，可以继续
    }

    // 只做色彩空间转换，准备好 YUV 数据 (源和目标尺寸都是视频原始尺寸)
    sws_scale(m_sws_context, (const uint8_t* const*)frame->data, frame->linesize,
            0, m_video_height, m_yuv_frame->data, m_yuv_frame->linesize);

    return true;
}

// 显示视频帧；在主线程中执行
void SDLVideoRenderer::displayFrame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_is_audio_only || !m_renderer || !m_texture || !m_yuv_frame) return;

    // 使用已准备好的 YUV 数据更新纹理
    SDL_UpdateYUVTexture(m_texture, nullptr,
                        m_yuv_frame->data[0], m_yuv_frame->linesize[0],
                        m_yuv_frame->data[1], m_yuv_frame->linesize[1],
                        m_yuv_frame->data[2], m_yuv_frame->linesize[2]);

    // 清空渲染器
    SDL_RenderClear(m_renderer);
    // 计算居中显示的矩形，让GPU在 RenderCopy() 时进行缩放
    SDL_Rect displayRect = calculateDisplayRect(m_window_width, m_window_height);
    SDL_RenderCopy(m_renderer, m_texture, nullptr, &displayRect);
    // 显示
    SDL_RenderPresent(m_renderer);
}

// 刷新纹理；在主线程中被调用
void SDLVideoRenderer::refresh() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_renderer || !m_window) return;

    // 根据模式选择不同的刷新行为
    if (m_is_audio_only) {
        // 在纯音频模式下，只需清屏以响应窗口事件（如尺寸调整）
        SDL_SetRenderDrawColor(m_renderer, 128, 128, 128, 255); // 深灰色背景
        SDL_RenderClear(m_renderer);
    }
    // 视频模式下的刷新逻辑
    else {
        // 如果没有有效的最后一帧，则只清屏
        if (!m_last_rendered_frame || m_last_rendered_frame->width == 0) {
            SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
            SDL_RenderClear(m_renderer);
        }
        // 如果有帧，就准备一个包含视频的画面
        else {
            // 使用 m_last_rendered_frame 的数据重新填充纹理，
            // 这样可以从系统造成的纹理内容丢失中恢复
            sws_scale(m_sws_context, (const uint8_t* const*)m_last_rendered_frame->data, m_last_rendered_frame->linesize,
                      0, m_video_height, m_yuv_frame->data, m_yuv_frame->linesize);
            
            SDL_UpdateYUVTexture(m_texture, nullptr,
                                m_yuv_frame->data[0], m_yuv_frame->linesize[0],
                                m_yuv_frame->data[1], m_yuv_frame->linesize[1],
                                m_yuv_frame->data[2], m_yuv_frame->linesize[2]);

            SDL_RenderClear(m_renderer);

            SDL_Rect displayRect = calculateDisplayRect(m_window_width, m_window_height);
            SDL_RenderCopy(m_renderer, m_texture, nullptr, &displayRect);
        }
    }

    SDL_RenderPresent(m_renderer);
    //std::cout << "SDLVideoRenderer: Display refreshed with last valid frame." << std::endl;
}

void SDLVideoRenderer::close() {
    std::lock_guard<std::mutex> lock(m_mutex);  // 加锁

    if (m_yuv_frame) {
        av_freep(&m_yuv_frame->data[0]); // 释放由 av_image_fill_arrays 分配的 buffer
        av_frame_free(&m_yuv_frame);
        m_yuv_frame = nullptr;
    }
    if (m_sws_context) {
        sws_freeContext(m_sws_context);
        m_sws_context = nullptr;
    }
    if (m_last_rendered_frame) {
        av_frame_free(&m_last_rendered_frame);
        m_last_rendered_frame = nullptr;
    }
    if (m_texture) {
        SDL_DestroyTexture(m_texture);
        m_texture = nullptr;
    }
    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

bool SDLVideoRenderer::onWindowResize(int newWidth, int newHeight) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_window_width = newWidth;
    m_window_height = newHeight;
    return true;
}

void SDLVideoRenderer::getWindowSize(int& width, int& height) const {
    if (m_window) {
        SDL_GetWindowSize(m_window, &width, &height);
    }
    else {
        width = m_window_width;
        height = m_window_height;
    }
}

SDL_Rect SDLVideoRenderer::calculateDisplayRect(int windowWidth, int windowHeight) const {
    SDL_Rect displayRect;

    // 计算视频和窗口的宽高比
    double videoAspect = (double)m_video_width / m_video_height;
    double windowAspect = (double)windowWidth / windowHeight;

    if (videoAspect > windowAspect) {
        // 视频比窗口更宽，以窗口宽度为准
        displayRect.w = windowWidth;
        displayRect.h = (int)(windowWidth / videoAspect);
        displayRect.x = 0;
        displayRect.y = (windowHeight - displayRect.h) / 2;
    }
    else {
        // 视频比窗口更高，以窗口高度为准
        displayRect.w = (int)(windowHeight * videoAspect);
        displayRect.h = windowHeight;
        displayRect.x = (windowWidth - displayRect.w) / 2;
        displayRect.y = 0;
    }

    return displayRect;
}
