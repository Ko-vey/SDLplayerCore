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
#include <memory>
#include <mutex>
#include <condition_variable>

// 前向声明 FFmpeg 类型
struct AVCodecParameters;
struct AVFrame;
struct AVPacket;
struct SwsContext;

// 接口头文件
#include "PacketQueue.h"    // 数据包队列
#include "FrameQueue.h"     // 数据帧队列
#include "IDemuxer.h"       // 解复用器
#include "IVideoDecoder.h"  // 视频解码器
#include "IAudioDecoder.h"  // 音频解码器
#include "IVideoRenderer.h" // 视频渲染器
#include "IAudioRenderer.h" // 音频渲染器
#include "IClockManager.h"  // 时钟管理器

using namespace std;

#define FF_REFRESH_EVENT (SDL_USEREVENT + 1)
#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h> // av_image_*()
#include <libavutil/error.h>    // 错误处理
}

#include "SDL2/SDL.h"
#include "SDL2/SDL_thread.h"

class MediaPlayer {
public: 
    // 播放器状态
    enum class PlayerState {
        IDLE,       // 空闲
        BUFFERING,  // 缓冲
        PLAYING,    // 播放
        PAUSED,     // 暂停
        STOPPED     // 停止/错误
    };

private:
    // 内部状态标志
    std::atomic<bool> m_quit;   // 退出标志
    std::atomic<PlayerState> m_playerState;  // 播放器核心状态
    std::atomic<bool> m_demuxer_eof; // 解复用器EOF标志
    std::mutex m_state_mutex;   // 用于保护状态转换和相关条件的互斥锁
    std::condition_variable m_state_cond;    // 用于唤醒因状态变化而等待的线程

    // 内部状态变量
    int videoStreamIndex;  // 解复用器找到的视频流索引
    int audioStreamIndex;  // 音频流索引
    // 其他变量
    int frame_cnt;         // 帧计数器
    std::atomic<int> m_seek_serial; // 全局序列号，用于播放"代际"隔离

    // 内部组件
    std::unique_ptr<PacketQueue> m_videoPacketQueue;
    std::unique_ptr<PacketQueue> m_audioPacketQueue;
    std::unique_ptr<FrameQueue> m_videoFrameQueue;
    std::unique_ptr<FrameQueue> m_audioFrameQueue;

    AVPacket* m_decodingVideoPacket;
    AVPacket* m_decodingAudioPacket;
    AVFrame* m_renderingVideoFrame;
    AVFrame* m_renderingAudioFrame;

    // 关系：MediaPlayer HAS-A IWorker
    std::unique_ptr<IDemuxer> m_demuxer;                // 解复用
    std::unique_ptr<IVideoDecoder> m_videoDecoder;      // 视频解码
    std::unique_ptr<IAudioDecoder> m_audioDecoder;      // 音频解码
    std::unique_ptr<IVideoRenderer> m_videoRenderer;    // 视频渲染
    std::unique_ptr<IAudioRenderer> m_audioRenderer;    // 音频渲染
    std::unique_ptr<IClockManager> m_clockManager;      // 时钟管理

    // 内部线程句柄
    SDL_Thread* m_demuxThread;        // 解复用
    SDL_Thread* m_videoDecodeThread;  // 视频解码
    SDL_Thread* m_audioDecodeThread;  // 音频解码
    SDL_Thread* m_videoRenderthread;  // 视频渲染
    SDL_Thread* m_audioRenderThread;  // 音频渲染
    SDL_Thread* m_controlThread;      // 总控制

    // --- 缓冲策略参数 (单位: 秒) ---
    // 当缓冲低于此值时，进入 BUFFERING 状态
    static constexpr double REBUFFER_THRESHOLD_SEC = 0.5;
    // 在 BUFFERING 状态下，缓冲超过此值时，恢复 PLAYING 状态
    static constexpr double PLAYOUT_THRESHOLD_SEC = 2.0;

public:
    MediaPlayer(const string& filepath);
    virtual ~MediaPlayer();

    MediaPlayer(const MediaPlayer& src) = delete;
    MediaPlayer& operator=(const MediaPlayer& rhs) = delete;

    int runMainLoop();      // 主循环启动函数
    int get_frame_cnt() const { return frame_cnt; };

private:
    // 线程入口函数
    // （为了兼容SDL API，包括静态入口和实际逻辑）
    static int demux_thread_entry(void* opaque);
    int demux_thread_func();
    static int video_decode_thread_entry(void* opaque);
    int video_decode_func();
    static int audio_decode_thread_entry(void* opaque);
    int audio_decode_func();
    static int video_render_thread_entry(void* opaque);
    int video_render_func();
    static int audio_render_thread_entry(void* opaque);
    int audio_render_func();
    static int control_thread_entry(void* opaque);
    int control_thread_func();

private:
    // 事件处理
    int handle_event(const SDL_Event& event);
    void resync_after_pause();
    // 辅助函数（构造和初始化）
    void init_components(const string& filepath);
    void init_ffmpeg_resources(const string& filepath);
    int init_demuxer_and_decoders(const string& filepath);
    void init_sdl_video_renderer();
    void init_sdl_audio_renderer();
    void start_threads();
    // 辅助函数（析构和资源清理）
    void cleanup_ffmpeg_resources();
    void cleanup();
};
