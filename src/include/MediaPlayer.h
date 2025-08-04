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

#include <string>
#include <iostream>
#include <atomic>
#include <memory>   //����ָ�룬std::unique_ptr
#include <mutex>
#include <condition_variable>

// ǰ������ FFmpeg ���ͣ��������ͷ�ļ�
struct AVCodecParameters;
struct AVFrame;
struct AVPacket;
struct SwsContext;

// �ӿ�ͷ�ļ�
#include "PacketQueue.h"    // ���ݰ�����
#include "FrameQueue.h"     // ����֡����
#include "IDemuxer.h"       // ���װ��
#include "IVideoDecoder.h"  // ��Ƶ������
#include "IAudioDecoder.h"  // ��Ƶ������
#include "IVideoRenderer.h" // ��Ƶ��Ⱦ��
#include "IAudioRenderer.h" // ��Ƶ��Ⱦ��
#include "IClockManager.h"  // ʱ�ӹ�����

using namespace std;

#define REFRESH_EVENT  (SDL_USEREVENT + 1)
#define BREAK_EVENT  (SDL_USEREVENT + 2)

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h> // av_image_*()
#include <libavutil/error.h>    // ������
}

#include "SDL2/SDL.h"
#include "SDL2/SDL_thread.h"

class MediaPlayer {
private:
    // �ڲ�״̬��־
    std::atomic<bool> m_quit;   //�˳���־
    std::atomic<bool> m_pause;  //��ͣ��־
    std::mutex m_pause_mutex;
    std::condition_variable m_pause_cond;
    // �ڲ����
    std::unique_ptr<PacketQueue> m_videoPacketQueue;    // ��Ƶ������
    std::unique_ptr<FrameQueue> m_videoFrameQueue;      // ��Ƶ֡����
    std::unique_ptr<PacketQueue> m_audioPacketQueue;    // ��Ƶ������
    std::unique_ptr<FrameQueue> m_audioFrameQueue;      // ��Ƶ֡����

    // ��ϵ��MediaPlayer HAS-A IWorker
    std::unique_ptr<IDemuxer> m_demuxer;                // ���װ��
    std::unique_ptr<IVideoDecoder> m_videoDecoder;      // ��Ƶ������
    std::unique_ptr<IAudioDecoder> m_audioDecoder;      // ��Ƶ������
    std::unique_ptr<IVideoRenderer> m_videoRenderer;    // ��Ƶ��Ⱦ��
    std::unique_ptr<IAudioRenderer> m_audioRenderer;    // ��Ƶ��Ⱦ��
    std::unique_ptr<IClockManager> m_clockManager;      // ʱ�ӹ�����

    // �ڲ�״̬����
    int videoStreamIndex = -1;                  // ���װ���ҵ�����Ƶ������
    int audioStreamIndex = -1;                  // ��Ƶ������
    // ��������
    int frame_cnt = 0;                          // ֡������

    AVPacket* m_decodingVideoPacket = nullptr;  // ���� ��Ƶ���� �� Packet 
    AVFrame* m_renderingVideoFrame = nullptr;   // ���� ��Ƶ��Ⱦ �� Frame
    AVPacket* m_decodingAudioPacket = nullptr;  // ���� ��Ƶ���� �� Packet
    AVFrame* m_renderingAudioFrame = nullptr;   // ���� ��Ƶ��Ⱦ �� Frame

    // �ڲ��߳̾��
    SDL_Thread* m_demuxThread = nullptr;        // ���װ�߳�
    SDL_Thread* m_videoDecodeThread = nullptr;  // ��Ƶ�����߳�
    SDL_Thread* m_videoRenderthread = nullptr;  // ��Ƶ��Ⱦ�߳�
    SDL_Thread* m_audioDecodeThread = nullptr;  // ��Ƶ�����߳�
    SDL_Thread* m_audioRenderThread = nullptr;  // ��Ƶ��Ⱦ�߳�

public:
    MediaPlayer(const string& filepath);
    virtual ~MediaPlayer();

    //���� �������캯�� �� ��ֵ����������
    MediaPlayer(const MediaPlayer& src) = delete;
    MediaPlayer& operator=(const MediaPlayer& rhs) = delete;

    int runMainLoop();      // ��ѭ����������
    int get_frame_cnt() const { return frame_cnt; };

private:
    // �߳���ں�������Ϊ ��̬��� �� ʵ���߼�
    static int demux_thread_entry(void* opaque);
    int demux_thread_func();
    static int video_decode_thread_entry(void* opaque);
    int video_decode_func();
    static int video_render_thread_entry(void* opaque);
    int video_render_func();
    static int audio_decode_thread_entry(void* opaque);
    int audio_decode_func();
    static int audio_render_thread_entry(void* opaque);
    int audio_render_func();

private:
    // �¼�����
    int handle_event(const SDL_Event& event);
    // ����ͳ�ʼ���ĸ�������
    void init_components(const string& filepath);
    void init_ffmpeg_resources(const string& filepath);
    int init_demuxer_and_decoders(const string& filepath);
    void init_sdl_video_renderer();
    void init_sdl_audio_renderer();
    void start_threads();
    // ������������Դ�ĸ�������
    void cleanup_ffmpeg_resources();
    void cleanup();
};
