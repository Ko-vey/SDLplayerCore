#pragma once

#include <string>
#include <iostream>
#include <atomic>
#include <memory>   //智能指针，std::unique_ptr
#include <mutex>
#include <condition_variable>

// 前向声明 FFmpeg 类型，用于相关头文件
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

#define REFRESH_EVENT  (SDL_USEREVENT + 1)
#define BREAK_EVENT  (SDL_USEREVENT + 2)

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
private:
    std::atomic<bool> m_quit;   //退出标志
    std::atomic<bool> m_pause;  //暂停标志
    std::mutex m_pause_mutex;
    std::condition_variable m_pause_cond;

    std::unique_ptr<PacketQueue> m_videoPacketQueue;    // 视频包队列
    std::unique_ptr<FrameQueue> m_videoFrameQueue;      // 视频帧队列
    std::unique_ptr<PacketQueue> m_audioPacketQueue;    // 音频包队列
    std::unique_ptr<FrameQueue> m_audioFrameQueue;      // 音频帧队列

    // 关系：MediaPlayer HAS-A IWorker
    std::unique_ptr<IDemuxer> m_demuxer;                // 解复用器
    std::unique_ptr<IVideoDecoder> m_videoDecoder;      // 视频解码器
    std::unique_ptr<IAudioDecoder> m_audioDecoder;      // 音频解码器
    std::unique_ptr<IVideoRenderer> m_videoRenderer;    // 视频渲染器
    std::unique_ptr<IAudioRenderer> m_audioRenderer;    // 音频渲染器
    std::unique_ptr<IClockManager> m_clockManager;      // 时钟管理器

    // 用于 解码 和 转换 的 FFmpeg 变量
    int videoStreamIndex = -1;                  // 解复用器找到的视频流索引
    int audioStreamIndex = -1;                  // 音频流索引
    
    AVPacket* m_decodingVideoPacket = nullptr;  // 用于 视频解码 的 Packet 
    AVFrame* m_renderingVideoFrame = nullptr;   // 用于 视频渲染 的 Frame
    AVPacket* m_decodingAudioPacket = nullptr;  // 用于 音频解码 的 Packet
    AVFrame* m_renderingAudioFrame = nullptr;   // 用于 音频渲染 的 Frame

    // SDL 线程
    SDL_Thread* m_demuxThread = nullptr;        // 解复用线程
    SDL_Thread* m_videoDecodeThread = nullptr;  // 视频解码线程
    SDL_Thread* m_videoRenderthread = nullptr;  // 视频渲染线程
    SDL_Thread* m_audioDecodeThread = nullptr;  // 音频解码线程
    SDL_Thread* m_audioRenderThread = nullptr;  // 音频渲染线程

    int frame_cnt = 0;                          // 帧计数器
public:
    MediaPlayer(const string& filepath);
    virtual ~MediaPlayer();

    //禁用 拷贝构造函数 和 赋值操作符重载
    MediaPlayer(const MediaPlayer& src) = delete;
    MediaPlayer& operator=(const MediaPlayer& rhs) = delete;

    int runMainLoop();      // 主循环启动函数
private:
    //线程入口函数，分为静态入口函数 和 实际逻辑
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

// 暂时公开，方便调试；当runMainLoop可以处理所有事件时，后继可改为private
public: 
    int handle_event(const SDL_Event& event);
    int show_frame_cnt() const{ return frame_cnt; };
    bool is_quit_requested() const { return m_quit; }
private:
    // 初始化方法的辅助函数
    void init_components(const string& filepath);
    void init_ffmpeg_resources(const string& filepath);
    int init_demuxer_and_decoders(const string& filepath);
    void init_sdl_video_renderer();
    void init_sdl_audio_renderer();
    void start_threads();
    // 清理资源的辅助方法
    void cleanup_ffmpeg_resources();
    void cleanup();
};
