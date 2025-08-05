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

// �����Ƶ֡����ʱ�ӿ죬�ȴ���
// �����Ƶ֡����ʱ�������������ֵ���룩������Ϊ����̫���ˡ�
constexpr double AV_SYNC_THRESHOLD_MIN = 0.04;
// �����Ƶ֡����ʱ�������������ֵ����С�����ֵ�����ٲ��ţ����ӳ٣���
constexpr double AV_SYNC_THRESHOLD_MAX = 0.1;
// �����Ƶ֡û���ṩ duration��ʹ�����Ĭ��ֵ����Ӧ25fps��
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

    m_decoder_pixel_format = decoderPixelFormat; // �������ظ�ʽ

    // ��¼��Ƶ�ʹ��ڵĳ�ʼ�ߴ�
    m_video_width = width;
    m_video_height = height;
    m_window_width = width;
    m_window_height = height;

    // ������ʼ��Դ
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

    // �ڴ���Ƶģʽ�£�����Ҫ Texture, SwsContext, �� YUV Frame
    // �����洰�ڳߴ���Ϣ
    m_window_width = width;
    m_window_height = height;

    // ����һ������ɫ�����γ���
    SDL_SetRenderDrawColor(m_renderer, 128, 128, 128, 255); // ���ɫ����
    SDL_RenderClear(m_renderer);
    SDL_RenderPresent(m_renderer);

    std::cout << "SDLVideoRenderer: Initialized in audio-only mode." << std::endl;
    return true;
}

void SDLVideoRenderer::setSyncParameters(AVRational time_base, double frame_rate) {
    m_time_base = time_base;
    if (frame_rate > 0) {
        // һ֡�ĳ���ʱ�䣨�룩
        m_frame_last_duration = 1.0 / frame_rate;
    }
    else {
        m_frame_last_duration = DEFAULT_FRAME_DURATION; // Ĭ��ֵ
    }
    m_frame_last_pts = 0.0;
}

bool SDLVideoRenderer::renderFrame(AVFrame* frame) {
    // ����Ǵ���Ƶģʽ��ֱ�ӷ��أ��������κ���Ƶ��Ⱦ����
    if (m_is_audio_only) return false;

    if (!frame || !m_clock_manager || !m_window || !m_renderer) return false;
    
    // --- 1������Ƶͬ���߼�  ---
    // ���㵱ǰ֡��PTS��duration���룩
    double pts;
    if (frame->pts == AV_NOPTS_VALUE) {
        pts = m_frame_last_pts;
    }
    else {
        // ���û��PTS���ͻ�����һ֡��PTS���й���
        pts = frame->pts * av_q2d(m_time_base);
    }
    if (pts == 0.0) { // ���PTSδ֪��Ϊ0��������һ֡���й���
        pts = m_frame_last_pts + m_frame_last_duration;
    }

    // ����֡�ĳ���ʱ�� duration���룩
    double duration;
    if (frame->duration > 0) {
        duration = frame->duration * av_q2d(m_time_base);
    }
    else {
        duration = m_frame_last_duration;
    }

    // ������Ƶʱ��
    m_clock_manager->setVideoClock(pts);

    // ������Ƶʱ������ʱ�ӵĲ�ֵ������ʱ��
    double delay = pts - m_clock_manager->getMasterClockTime();

    // ������ʱ����ͬ������
    const double AV_SYNC_THRESHOLD_MIN = 0.04; // ͬ����ֵ���� (40ms)
    const double AV_SYNC_THRESHOLD_MAX = 0.1;  // ͬ����ֵ���� (100ms)
    const double AV_NOSYNC_THRESHOLD = 10.0;   // ��ͬ�������ã���ֵ (10s)

    // �����ʱ�Ƿ���󣬹�������Ϊʱ�Ӳ�ͬ����������ʱ
    if (delay > AV_NOSYNC_THRESHOLD || delay < -AV_NOSYNC_THRESHOLD) {
        // ʱ�Ӳ����󣬿��ܳ����ˣ�����ʱ��
        std::cout << "VideoRenderer: Clock difference is too large (" << delay << "s), resetting delay." << std::endl;
        delay = 0;
    }

    if (delay > 0) {
        // ��Ƶ��ǰ (video is early)����Ҫ�ȴ�
        // �ȴ�ʱ��ȡ delay ��������ֵ�еĽ�С�ߣ���ֹ��ʱ�����䵼�³�ʱ������
        double wait_time = std::min(delay, AV_SYNC_THRESHOLD_MAX);
        SDL_Delay(static_cast<Uint32>(wait_time * 1000.0));
    }
    // �����Ƶ��� (video is late)��������̫�� (delay > -AV_SYNC_THRESHOLD_MIN)����������ʾ
    // �����Ƶ���̫�� (delay < -AV_SYNC_THRESHOLD)�����Կ��Ƕ�֡���˴���Ϊ����֡��������Ⱦ��
    // if (delay < -AV_SYNC_THRESHOLD_MIN) { /* �˴�����Ӷ�֡�߼� */ }

    // --- 2��SDL��Ⱦ�߼� ---
    // ������������������Ⱦ��Դ
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_renderer || !m_texture || !m_sws_context) return false;

    // ����Ⱦǰ���Ƚ���ǰ֡���ݿ�¡�� m_last_rendered_frame
    // ���� refresh() �����������µ���Ч֡����
    av_frame_unref(m_last_rendered_frame); // �ͷžɵ�����
    if (av_frame_ref(m_last_rendered_frame, frame) < 0) {
        std::cerr << "SDLVideoRenderer: Failed to reference last frame." << std::endl;
        // ���������󣬿��Լ���
    }

    // ɫ�ʿռ�ת�� (ֱ�����ŵ�������ĳߴ�)
    if (sws_scale(m_sws_context, (const uint8_t* const*)frame->data, frame->linesize,
        0, m_video_height, m_yuv_frame->data, m_yuv_frame->linesize) < 0) {
        std::cerr << "SDLVideoRenderer: Error in sws_scale." << std::endl;
        return false;
    }

    // ��������
    SDL_UpdateYUVTexture(m_texture, nullptr,
        m_yuv_frame->data[0], m_yuv_frame->linesize[0],
        m_yuv_frame->data[1], m_yuv_frame->linesize[1],
        m_yuv_frame->data[2], m_yuv_frame->linesize[2]);

    // �����Ⱦ��
    SDL_RenderClear(m_renderer);

    // ������ʾ���Σ����ھ��У�
    SDL_Rect displayRect = calculateDisplayRect(m_window_width, m_window_height);
    
    // �������Ƶ�������ľ���λ��
    SDL_RenderCopy(m_renderer, m_texture, nullptr, &displayRect);

    // ��ʾ
    SDL_RenderPresent(m_renderer);

    // --- 3������״̬ ---
    // �ɹ���Ⱦ�󣬱����������Ч��
    m_texture_lost = false; 
    // ������һ֡����Ϣ��������һ��ѭ���Ĺ���
    m_frame_last_pts = pts;
    m_frame_last_duration = duration;

    return true;
}

void SDLVideoRenderer::requestRefresh() {
    // ��������ԭ�Ӳ������������̰߳�ȫ
    m_refresh_requested = true;
}

// ˢ������
void SDLVideoRenderer::refresh() {
    // ���ñ�־λ
    m_refresh_requested = false;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_renderer || !m_window) return;

    // ����ģʽѡ��ͬ��ˢ����Ϊ
    if (m_is_audio_only) {
        // �ڴ���Ƶģʽ�£�ֻ����������Ӧ�����¼�����ߴ������
        SDL_SetRenderDrawColor(m_renderer, 128, 128, 128, 255); // ���ɫ����
        SDL_RenderClear(m_renderer);
    }
    // ��Ƶģʽ�µ�ˢ���߼�
    else {
        // ���û����Ч�����һ֡����ֻ����
        if (!m_last_rendered_frame || m_last_rendered_frame->width == 0) {
            SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
            SDL_RenderClear(m_renderer);
        }
        // �����֡����׼��һ��������Ƶ�Ļ���
        else {
            // ʹ�� m_last_rendered_frame �����������������
            // �������Դ�ϵͳ��ɵ��������ݶ�ʧ�лָ�
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
    std::lock_guard<std::mutex> lock(m_mutex);  // ����

    if (m_yuv_frame) {
        av_freep(&m_yuv_frame->data[0]); // �ͷ���av_image_fill_arrays�����buffer
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
    std::lock_guard<std::mutex> lock(m_mutex);  // ����

    if (!m_window || !m_renderer) {
        return false;
    }

    // ���´��ڴ�С��¼
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

    // ������Ƶ�ʹ��ڵĿ�߱�
    double videoAspect = (double)m_video_width / m_video_height;
    double windowAspect = (double)windowWidth / windowHeight;

    if (videoAspect > windowAspect) {
        // ��Ƶ�ȴ��ڸ����Դ��ڿ��Ϊ׼
        displayRect.w = windowWidth;
        displayRect.h = (int)(windowWidth / videoAspect);
        displayRect.x = 0;
        displayRect.y = (windowHeight - displayRect.h) / 2;
    }
    else {
        // ��Ƶ�ȴ��ڸ��ߣ��Դ��ڸ߶�Ϊ׼
        displayRect.w = (int)(windowHeight * videoAspect);
        displayRect.h = windowHeight;
        displayRect.x = (windowWidth - displayRect.w) / 2;
        displayRect.y = 0;
    }

    return displayRect;
}

bool SDLVideoRenderer::recreateResources() {
    // �ú���Ӧ���ڳ�����������±�����

    // 1. ���پɵ���Ƶ�����Դ
    if (m_texture) {
        SDL_DestroyTexture(m_texture);
        m_texture = nullptr;
    }
    if (m_sws_context) {
        sws_freeContext(m_sws_context);
        m_sws_context = nullptr;
    }
    // ���پɵ� YUV ֡���仺����
    if (m_yuv_frame) {
        av_freep(&m_yuv_frame->data[0]); // �ͷ��� av_image_fill_arrays ����� buffer
        av_frame_free(&m_yuv_frame);
        m_yuv_frame = nullptr;
    }

    // 2. ���ݵ�ǰ���ڴ�С�����µ���ʾ����
    SDL_Rect displayRect = calculateDisplayRect(m_window_width, m_window_height);
    int newTextureWidth = displayRect.w;
    int newTextureHeight = displayRect.h;

    // 3. �����µ�Texture����ߴ�Ϊ������Ⱦ��Ŀ��ߴ�
    m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, 
                                  newTextureWidth, newTextureHeight);
    if (!m_texture) {
        std::cerr << "Texture could not be recreated! SDL_Error: " << SDL_GetError() << std::endl;
        return false;
    }

    // 4. �����µ�SwsContext������Ƶԭʼ�ߴ�ֱ�����ŵ��µ�Texture�ߴ�
    m_sws_context = sws_getContext(m_video_width, m_video_height, m_decoder_pixel_format, // ��Ҫ��������������ظ�ʽ
                                    newTextureWidth, newTextureHeight, AV_PIX_FMT_YUV420P,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws_context) {
        std::cerr << "Could not recreate SwsContext" << std::endl;
        return false;
    }

    // 5. �����µ� Texture �ߴ磬���´��� YUV ֡�ͻ�����
    m_yuv_frame = av_frame_alloc();
    if (!m_yuv_frame) {
        std::cerr << "Could not allocate YUV frame" << std::endl;
        return false;
    }
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, newTextureWidth, newTextureHeight, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
    if (!buffer) { 
        std::cerr << "Could not allocate memory block buffer" << std::endl;
        av_frame_free(&m_yuv_frame); // �ڷ���ǰ���ͷ��ѷ���� m_yuv_frame �ṹ��
        m_yuv_frame = nullptr;
        return false; 
    }
    av_image_fill_arrays(m_yuv_frame->data, m_yuv_frame->linesize, buffer, AV_PIX_FMT_YUV420P, newTextureWidth, newTextureHeight, 1);

    // ����������ĳߴ磬�� renderFrame ��
    m_yuv_frame->width = newTextureWidth;
    m_yuv_frame->height = newTextureHeight;

    std::cout << "SDLVideoRenderer: Recreated resources for size " << newTextureWidth << "x" << newTextureHeight << std::endl;
    return true;
}
