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

    // ���㱣�ֿ�߱ȵ���ʾ����
    SDL_Rect calculateDisplayRect(int windowWidth, int windowHeight) const;

    std::mutex m_mutex; // ���ڱ�����SDL��Դ�ķ���

    // �������һ֡�ĸ���������ˢ�ºͻָ�
    AVFrame* m_last_rendered_frame = nullptr;
    bool m_texture_lost = false; // ������������Ƿ�����Ѷ�ʧ

    bool m_is_audio_only = false;   // ����Ƿ�Ϊ����Ƶģʽ

public:
    SDLVideoRenderer() = default;
    virtual ~SDLVideoRenderer();

    // �ӿ�Ҫ��ĳ�ʼ������
    bool init(const char* windowTitle, int width, int height,
        enum AVPixelFormat decoderPixelFormat, IClockManager* clockManager) override;

    /**
     * @brief Ϊ��Ⱦ�����ùؼ���ͬ��������
     * �����ڵ��� renderFrame() ֮ǰ���á�
     * @param time_base ��FFmpeg demuxer��ȡ����Ƶ��ʱ�����
     * @param frame_rate ��Ƶ��ƽ��֡�ʣ����ڹ���֡����ʱ�䡣
     */
    void setSyncParameters(AVRational time_base, double frame_rate);

    bool renderFrame(AVFrame* frame) override;
    void close() override;
    void refresh() override;

    bool onWindowResize(int newWidth, int newHeight) override;
    void getWindowSize(int& width, int& height) const override;

    /**
    * @brief ��ȡ����ָ��ķ������Թ��ⲿ��鴰��״̬
    */
    SDL_Window* getWindow() const { return m_window; }

    /**
    * @brief Ϊ����Ƶ����ģʽ��Ƶĳ�ʼ����������ʼ��һ�����ں���Ⱦ��������������Ƶ�����Դ
    */
    bool initForAudioOnly(const char* windowTitle, int width, int height, IClockManager* clockManager);
};
