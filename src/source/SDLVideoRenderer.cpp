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
#include <algorithm> // For std::max
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

    m_window = SDL_CreateWindow(windowTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        width, height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!m_window) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        return false;
    }

    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
    if (!m_renderer) {
        std::cerr << "Could not create accelerated renderer, falling back to software. Error: " << SDL_GetError() << std::endl;
        m_renderer = SDL_CreateRenderer(m_window, -1, 0);
        if (!m_renderer) {
            std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
            return false;
        }
    }

    m_decoder_pixel_format = decoderPixelFormat; // 保存像素格式

    // 记录视频和窗口的初始尺寸
    m_video_width = width;
    m_video_height = height;
    m_window_width = width;
    m_window_height = height;

    // 创建初始资源
    if (!recreateResources()) {
        return false;
    }

    m_last_rendered_frame = av_frame_alloc();
    if (!m_last_rendered_frame) {
        std::cerr << "Could not allocate last rendered frame" << std::endl;
        return false;
    }

    m_clock_manager = clockManager;
    return true;
}

bool SDLVideoRenderer::initForAudioOnly(const char* windowTitle, int width, int height, IClockManager* clockManager) {
    m_is_audio_only = true;
    m_clock_manager = clockManager;

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

    // 在纯音频模式下，不需要 Texture, SwsContext, 或 YUV Frame
    // 仅保存窗口尺寸信息
    m_window_width = width;
    m_window_height = height;

    // 设置一个背景色并初次呈现
    SDL_SetRenderDrawColor(m_renderer, 128, 128, 128, 255); // 深灰色背景
    SDL_RenderClear(m_renderer);
    SDL_RenderPresent(m_renderer);

    std::cout << "SDLVideoRenderer: Initialized in audio-only mode." << std::endl;
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

bool SDLVideoRenderer::renderFrame(AVFrame* frame) {
    // 如果是纯音频模式，直接返回，不进行任何视频渲染操作
    if (m_is_audio_only) return false;

    if (!frame || !m_clock_manager || !m_window || !m_renderer) return false;
    
    // --- 1、音视频同步逻辑  ---
    // 计算当前帧的PTS和duration（秒）
    double pts;
    if (frame->pts == AV_NOPTS_VALUE) {
        pts = m_frame_last_pts;
    }
    else {
        // 如果没有PTS，就基于上一帧的PTS进行估算
        pts = frame->pts * av_q2d(m_time_base);
    }
    if (pts == 0.0) { // 如果PTS未知或为0，基于上一帧进行估算
        pts = m_frame_last_pts + m_frame_last_duration;
    }

    // 计算帧的持续时间 duration（秒）
    double duration;
    if (frame->duration > 0) {
        duration = frame->duration * av_q2d(m_time_base);
    }
    else {
        duration = m_frame_last_duration;
    }

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

    if (delay > 0) {
        // 视频超前 (video is early)，需要等待
        // 等待时间取 delay 和上限阈值中的较小者，防止因时钟跳变导致长时间阻塞
        double wait_time = std::min(delay, AV_SYNC_THRESHOLD_MAX);
        SDL_Delay(static_cast<Uint32>(wait_time * 1000.0));
    }
    // 如果视频落后 (video is late)，但不是太多 (delay > -AV_SYNC_THRESHOLD_MIN)，则立即显示
    // 如果视频落后太多 (delay < -AV_SYNC_THRESHOLD)，可以考虑丢帧。此处简化为不丢帧，总是渲染。
    // if (delay < -AV_SYNC_THRESHOLD_MIN) { /* 此处可添加丢帧逻辑 */ }

    // --- 2、SDL渲染逻辑 ---
    // 锁定互斥锁，保护渲染资源
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_renderer || !m_texture || !m_sws_context) return false;

    // 在渲染前，先将当前帧数据克隆到 m_last_rendered_frame
    // 这样 refresh() 就总是有最新的有效帧可用
    av_frame_unref(m_last_rendered_frame); // 释放旧的引用
    if (av_frame_ref(m_last_rendered_frame, frame) < 0) {
        std::cerr << "SDLVideoRenderer: Failed to reference last frame." << std::endl;
        // 非致命错误，可以继续
    }

    // 色彩空间转换 (直接缩放到新纹理的尺寸)
    if (sws_scale(m_sws_context, (const uint8_t* const*)frame->data, frame->linesize,
        0, m_video_height, m_yuv_frame->data, m_yuv_frame->linesize) < 0) {
        std::cerr << "SDLVideoRenderer: Error in sws_scale." << std::endl;
        return false;
    }

    // 更新纹理
    SDL_UpdateYUVTexture(m_texture, nullptr,
        m_yuv_frame->data[0], m_yuv_frame->linesize[0],
        m_yuv_frame->data[1], m_yuv_frame->linesize[1],
        m_yuv_frame->data[2], m_yuv_frame->linesize[2]);

    // 清空渲染器
    SDL_RenderClear(m_renderer);

    // 计算显示矩形（用于居中）
    SDL_Rect displayRect = calculateDisplayRect(m_window_width, m_window_height);
    
    // 将纹理复制到计算出的矩形位置
    SDL_RenderCopy(m_renderer, m_texture, nullptr, &displayRect);

    // 显示
    SDL_RenderPresent(m_renderer);

    // --- 3、更新状态 ---
    // 成功渲染后，标记纹理是有效的
    m_texture_lost = false; 
    // 更新上一帧的信息，用于下一次循环的估算
    m_frame_last_pts = pts;
    m_frame_last_duration = duration;

    return true;
}

void SDLVideoRenderer::requestRefresh() {
    // 非阻塞的原子操作，快速且线程安全
    m_refresh_requested = true;
}

// 刷新纹理
void SDLVideoRenderer::refresh() {
    // 重置标志位
    m_refresh_requested = false;

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
            // 使用 m_last_rendered_frame 的数据重新填充纹理
            // 这样可以从系统造成的纹理内容丢失中恢复
            if (sws_scale(m_sws_context, (const uint8_t* const*)m_last_rendered_frame->data, m_last_rendered_frame->linesize,
                0, m_video_height, m_yuv_frame->data, m_yuv_frame->linesize) < 0) {
                std::cerr << "SDLVideoRenderer: Error in sws_scale during refresh." << std::endl;
                return;
            }

            SDL_UpdateYUVTexture(m_texture, nullptr,
                m_yuv_frame->data[0], m_yuv_frame->linesize[0],
                m_yuv_frame->data[1], m_yuv_frame->linesize[1],
                m_yuv_frame->data[2], m_yuv_frame->linesize[2]);

            SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
            SDL_RenderClear(m_renderer);

            int currentWindowWidth, currentWindowHeight;
            SDL_GetWindowSize(m_window, &currentWindowWidth, &currentWindowHeight);

            SDL_Rect displayRect = calculateDisplayRect(currentWindowWidth, currentWindowHeight);
            SDL_RenderCopy(m_renderer, m_texture, nullptr, &displayRect);
        }
    }

    SDL_RenderPresent(m_renderer);
    //std::cout << "SDLVideoRenderer: Display refreshed with last valid frame." << std::endl;
}

void SDLVideoRenderer::close() {
    std::lock_guard<std::mutex> lock(m_mutex);  // 加锁

    if (m_yuv_frame) {
        av_freep(&m_yuv_frame->data[0]); // 释放由av_image_fill_arrays分配的buffer
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
    std::lock_guard<std::mutex> lock(m_mutex);  // 加锁

    if (!m_window || !m_renderer) {
        return false;
    }

    // 更新窗口大小记录
    m_window_width = newWidth;
    m_window_height = newHeight;

    return recreateResources();
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

bool SDLVideoRenderer::recreateResources() {
    // 该函数应该在持有锁的情况下被调用

    // 1. 销毁旧的视频相关资源
    if (m_texture) {
        SDL_DestroyTexture(m_texture);
        m_texture = nullptr;
    }
    if (m_sws_context) {
        sws_freeContext(m_sws_context);
        m_sws_context = nullptr;
    }
    // 销毁旧的 YUV 帧和其缓冲区
    if (m_yuv_frame) {
        av_freep(&m_yuv_frame->data[0]); // 释放由 av_image_fill_arrays 分配的 buffer
        av_frame_free(&m_yuv_frame);
        m_yuv_frame = nullptr;
    }

    // 2. 根据当前窗口大小计算新的显示矩形
    SDL_Rect displayRect = calculateDisplayRect(m_window_width, m_window_height);
    int newTextureWidth = displayRect.w;
    int newTextureHeight = displayRect.h;

    // 3. 创建新的Texture，其尺寸为最终渲染的目标尺寸
    m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, 
                                  newTextureWidth, newTextureHeight);
    if (!m_texture) {
        std::cerr << "Texture could not be recreated! SDL_Error: " << SDL_GetError() << std::endl;
        return false;
    }

    // 4. 创建新的SwsContext，从视频原始尺寸直接缩放到新的Texture尺寸
    m_sws_context = sws_getContext(m_video_width, m_video_height, m_decoder_pixel_format, // 需要保存解码器的像素格式
                                    newTextureWidth, newTextureHeight, AV_PIX_FMT_YUV420P,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws_context) {
        std::cerr << "Could not recreate SwsContext" << std::endl;
        return false;
    }

    // 5. 根据新的 Texture 尺寸，重新创建 YUV 帧和缓冲区
    m_yuv_frame = av_frame_alloc();
    if (!m_yuv_frame) {
        std::cerr << "Could not allocate YUV frame" << std::endl;
        return false;
    }
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, newTextureWidth, newTextureHeight, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
    if (!buffer) { 
        std::cerr << "Could not allocate memory block buffer" << std::endl;
        av_frame_free(&m_yuv_frame); // 在返回前，释放已分配的 m_yuv_frame 结构体
        m_yuv_frame = nullptr;
        return false; 
    }
    av_image_fill_arrays(m_yuv_frame->data, m_yuv_frame->linesize, buffer, AV_PIX_FMT_YUV420P, newTextureWidth, newTextureHeight, 1);

    // 保存新纹理的尺寸，给 renderFrame 用
    m_yuv_frame->width = newTextureWidth;
    m_yuv_frame->height = newTextureHeight;

    std::cout << "SDLVideoRenderer: Recreated resources for size " << newTextureWidth << "x" << newTextureHeight << std::endl;
    return true;
}
