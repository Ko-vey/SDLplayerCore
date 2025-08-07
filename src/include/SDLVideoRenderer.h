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
 * @brief IVideoRenderer��һ��ʵ�֣�ʹ��SDL2������Ƶ��Ⱦ��
 *
 * ������װ��SDL���ڡ���Ⱦ��������Ĵ����͹���
 * ���Ĺ�����renderFrame()�У�����������IClockManagerЭ������Ƶͬ���߼���
 */
class SDLVideoRenderer : public IVideoRenderer {
private:
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture* m_texture = nullptr;
    SwsContext* m_sws_context = nullptr;
    AVFrame* m_yuv_frame = nullptr;

    IClockManager* m_clock_manager = nullptr;
    AVRational m_time_base;         // ��Ƶ����ʱ���������PTS����

    double m_frame_last_pts = 0.0;      // ��һ֡��PTS
    double m_frame_last_duration = 0.04; // ֡����ʱ��Ĺ���ֵ (Ĭ��25fps)
    int m_video_width = 0;      // ��Ƶԭʼ���
    int m_video_height = 0;     // ��Ƶԭʼ�߶�
    int m_window_width = 0;     // ��ǰ���ڿ��
    int m_window_height = 0;    // ��ǰ���ڸ߶�

    // �����ݴ����õĳ�Ա����
    std::string m_window_title;
    enum AVPixelFormat m_decoder_pixel_format;
    bool m_is_audio_only = false;   // ����Ƿ�Ϊ����Ƶģʽ

    // ���㱣�ֿ�߱ȵ���ʾ����
    SDL_Rect calculateDisplayRect(int windowWidth, int windowHeight) const;

    std::mutex m_mutex;                         // ���ڱ�����SDL��Դ�ķ���
    AVFrame* m_last_rendered_frame = nullptr;   // �������һ֡�ĸ���������ˢ�ºͻָ�

public:
    SDLVideoRenderer() = default;
    virtual ~SDLVideoRenderer();

    bool init(const char* windowTitle, int width, int height,
              enum AVPixelFormat decoderPixelFormat, IClockManager* clockManager) override;

    /**
     * @brief Ϊ��Ⱦ�����ùؼ���ͬ��������
     * @param time_base �ӽ��װ����ȡ����Ƶ��ʱ���
     * @param frame_rate ��Ƶ��ƽ��֡�ʣ����ڹ���֡����ʱ��
     */
    void setSyncParameters(AVRational time_base, double frame_rate);

    // ��Ⱦ�߼���ط���
    double calculateSyncDelay(AVFrame* frame) override;
    bool prepareFrameForDisplay(AVFrame* frame) override;
    void displayFrame() override; // �����߳��е���

    void close() override;
    void refresh() override;

    bool onWindowResize(int newWidth, int newHeight) override;
    void getWindowSize(int& width, int& height) const override;
};
