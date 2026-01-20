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

    // 初始化 OSD
    m_osd_layer = std::make_unique<OSDLayer>();
    // 注意：这里的字体路径可能需要根据实际情况调整
    if (!m_osd_layer->init("C:/Windows/Fonts/arial.ttf")) {
        std::cerr << "Warning: Failed to init OSD font." << std::endl;
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

void SDLVideoRenderer::setDebugStats(std::shared_ptr<PlayerDebugStats> stats) {
    m_debug_stats = stats;
}

void SDLVideoRenderer::renderOSD() {
    if (m_osd_layer && m_debug_stats) {
        int w, h;
        // 获取当前渲染器输出大小（即窗口大小）
        if (SDL_GetRendererOutputSize(m_renderer, &w, &h) == 0) {
            m_osd_layer->render(m_renderer, *m_debug_stats, w, h);
        }
    }
}

// 在工作线程中执行
double SDLVideoRenderer::calculateSyncDelay(AVFrame* frame) {
    if (!frame || !m_clock_manager) return 0.0;

    // 1. 计算当前帧的PTS
    double pts;
    if (frame->pts != AV_NOPTS_VALUE) {
        // 如果没有PTS，就基于上一帧的PTS进行估算
        pts = frame->pts * av_q2d(m_time_base);
    }
    else {
        // 如果PTS未知或为0，基于上一帧进行估算
        pts = m_frame_last_pts + m_frame_last_duration;
    }

    // 计算 duration
    double duration = (frame->duration > 0) ? (frame->duration * av_q2d(m_time_base)) : m_frame_last_duration;

    // 更新上一帧的信息，用于下一次循环的估算
    m_frame_last_pts = pts;
    m_frame_last_duration = duration;

    // 更新视频时钟
    m_clock_manager->setVideoClock(pts);

    // 处理 Reset 后的第一帧
    // 如果是刚恢复播放的第一帧，无论 Delay 多少，都强制立即渲染
    // (消除由于旧 PTS 残留导致的大数 Delay 误判)
    if (m_first_frame_after_reset) {
        m_first_frame_after_reset = false;
        // 如果 Audio 已经跑了，syncToPts 可能无效(被 Audio 覆盖)，但至少应该返回 0.0
        if (m_clock_manager->isClockUnknown()) {
            m_clock_manager->syncToPts(pts);
        }
        std::cout << "VideoRenderer: First frame after reset. Force render. PTS: " << pts << std::endl;
        return 0.0;
    }

    // 处理时钟未同步状态 (例如网络流恢复后的第一帧)
    if (m_clock_manager->isClockUnknown()) {
        // 第一帧即为标准。命令时钟管理器以其 PTS 为基准开始计时。
        m_clock_manager->syncToPts(pts);

        // 刚校准完毕，时钟位于 pts，delay = 0。
        // 直接返回 0，让这一帧立即显示。
        std::cout << "VideoRenderer: Clock was unknown. Synced to frame PTS: " << pts << std::endl;
        return 0.0;
    }

    // 2. 同步计算逻辑
    double master_clock = m_clock_manager->getMasterClockTime();

    // 双重保险：万一 syncToPts 没生效或者其他原因导致 master_clock 依然是 NAN
    if (std::isnan(master_clock)) {
        // 直接播放，防止逻辑卡死
        return 0.0;
    }

    // 计算视频时钟与主时钟的差值（延时）
    double delay = pts - master_clock;

    // --- 更新 OSD 数据 ---
    if (m_debug_stats) {
        m_debug_stats->av_diff_ms = delay * 1000.0; // 秒转毫秒
        m_debug_stats->video_current_pts = pts;
        m_debug_stats->master_clock_val = master_clock;
        m_debug_stats->clock_source_type = static_cast<int>(m_clock_manager->getMasterClockType());
    }

    // 默认阈值 10秒 (本地文件)
    // 直播流不能容忍大的时间轴断裂，必须收紧阈值
    double sync_threshold = m_is_live_stream ? 1.0 : 10.0;

    // 检查延时是否过大
    if (std::abs(delay) > sync_threshold) {
        // 获取当前时钟类型
        MasterClockType currentClockType = m_clock_manager->getMasterClockType();

        // 只有当主时钟不是音频时，视频渲染器才有权强行校准时钟
        if (currentClockType != MasterClockType::AUDIO) {
            std::cout << "VideoRenderer: Clock diff too large (" << delay
                << "s > threshold " << sync_threshold << "s). Resyncing." << std::endl;

            m_clock_manager->syncToPts(pts);
            return 0.0; // 立即渲染
        }
        else {
            // 如果主时钟是 AUDIO，且差距巨大：
            // 1. 若 delay > 0 (视频超前): 不改音频时钟。让逻辑继续往下走，
            //    下方的 delay > AV_SYNC_THRESHOLD_MAX，返回最大等待时间。
            //    视频会“停顿”等待音频追上来。
            // 2. 若 delay < 0 (视频落后): 让逻辑继续往下走，
            //    下方的 SYNC_SIGNAL_DROP_FRAME，触发快速丢帧追赶。

            // 仅打印日志，不操作
            std::cout << "VideoRenderer: Large gap in Audio Mode. Waiting/Dropping..." << std::endl;
        }
    }

    // 视频严重落后，请求丢帧
    if (delay < -AV_SYNC_THRESHOLD_MAX) {
        // 返回一个特殊信号，通知调用者丢弃此帧
        std::cout << "VideoRenderer: Lagging significantly (" << delay << "s). Requesting frame drop." << std::endl;
        return SYNC_SIGNAL_DROP_FRAME;
    }

    // 如果视频帧滞后但未超标，全速渲染，不丢包也不等待
    if (delay < 0) {
        return 0.0;
    }

    // 在“同步区”内 (微小落后或微小超前)，则认为无需等待，立即显示
    if (delay < AV_SYNC_THRESHOLD_MIN) {
        return 0.0;
    }

    // 如果视频超前太多，则截断等待时间，防止因时钟突变导致长时间卡顿
    if (delay > AV_SYNC_THRESHOLD_MAX) { 
        return AV_SYNC_THRESHOLD_MAX;
    }

    // 默认情况：视频在合理范围内超前，返回需要等待的精确时间
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

    // 更新渲染帧率
    if (m_debug_stats) {
        m_debug_stats->render_fps.tick();
    }
    // 绘制OSD层
    renderOSD();
    // 显示
    SDL_RenderPresent(m_renderer);
}

// 刷新纹理；在主线程中被调用
void SDLVideoRenderer::refresh() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_renderer || !m_window) return;

    // 纯音频模式下的刷新行为
    if (m_is_audio_only) {
        // 只需清屏以响应窗口事件（如尺寸调整）
        SDL_SetRenderDrawColor(m_renderer, 128, 128, 128, 255); // 深灰色背景
        SDL_RenderClear(m_renderer);
    }
    // 视频模式
    else {
        // 如果没有有效的最后一帧，则只清屏
        if (!m_last_rendered_frame || m_last_rendered_frame->width == 0) {
            SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
            SDL_RenderClear(m_renderer);
        }
        // 如果有帧，就准备一个包含视频的画面
        else {
            // 直接使用现有纹理进行重绘
            SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
            SDL_RenderClear(m_renderer); // 清除背景
            
            SDL_Rect displayRect = calculateDisplayRect(m_window_width, m_window_height);

            // 绘制现有的纹理
            int ret = SDL_RenderCopy(m_renderer, m_texture, nullptr, &displayRect);

            // 如果绘制失败（极少，如上下文丢失），尝试恢复数据并重绘
            if (ret < 0) {
                std::cerr << "SDLVideoRenderer: RenderCopy failed (" << SDL_GetError() << "), attempting to reload texture..." << std::endl;

                if (m_yuv_frame && m_sws_context && m_last_rendered_frame) {
                    // 重新进行格式转换
                    sws_scale(m_sws_context, (const uint8_t* const*)m_last_rendered_frame->data, m_last_rendered_frame->linesize,
                        0, m_video_height, m_yuv_frame->data, m_yuv_frame->linesize);

                    // 重新上传数据到纹理
                    SDL_UpdateYUVTexture(m_texture, nullptr,
                        m_yuv_frame->data[0], m_yuv_frame->linesize[0],
                        m_yuv_frame->data[1], m_yuv_frame->linesize[1],
                        m_yuv_frame->data[2], m_yuv_frame->linesize[2]);

                    // 数据恢复后，必须再次调用 RenderCopy
                    if (SDL_RenderCopy(m_renderer, m_texture, nullptr, &displayRect) < 0) {
                        std::cerr << "SDLVideoRenderer: Recovery failed. Texture might be invalid." << std::endl;
                    }
                }
            }
        }
    }
    renderOSD();

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

void SDLVideoRenderer::setStreamType(bool isLive) { 
    m_is_live_stream = isLive; 
}

void SDLVideoRenderer::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    // 重置上一帧 PTS 记录，防止新流的 PTS 与旧流混淆
    m_frame_last_pts = 0.0;
    m_frame_last_duration = DEFAULT_FRAME_DURATION;
    // 标记 reset 状态
    m_first_frame_after_reset = true;
    std::cout << "SDLVideoRenderer: Flushed internal state." << std::endl;
}