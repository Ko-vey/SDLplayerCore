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

#include <fstream>      // 文件路径验证
#include <stdexcept>    // std::runtime_error
#include <chrono>       // SDL_Delay 或者 PacketQueue 超时

// PacketQueue.h 和 FrameQueue.h 通过 MediaPlayer.h 导入
#include "../include/MediaPlayer.h"
#include "../include/FFmpegDemuxer.h"
#include "../include/FFmpegVideoDecoder.h"
#include "../include/FFmpegAudioDecoder.h"
#include "../include/SDLVideoRenderer.h"
#include "../include/SDLAudioRenderer.h"
#include "../include/ClockManager.h"

using namespace std;

// 初始化所有组件并启动工作线程，失败时抛出 std::runtime_error
MediaPlayer::MediaPlayer(const string& filepath) :
    m_quit(false),
    m_playerState(PlayerState::BUFFERING),
    m_demuxer_eof(false),
    videoStreamIndex(-1),
    audioStreamIndex(-1),
    frame_cnt(0),
    m_videoPacketQueue(nullptr),
    m_videoFrameQueue(nullptr),
    m_audioPacketQueue(nullptr),
    m_audioFrameQueue(nullptr),
    m_decodingVideoPacket(nullptr),
    m_renderingVideoFrame(nullptr),
    m_decodingAudioPacket(nullptr),
    m_renderingAudioFrame(nullptr),
    m_demuxer(nullptr),
    m_videoDecoder(nullptr),
    m_audioDecoder(nullptr),
    m_videoRenderer(nullptr),
    m_audioRenderer(nullptr),
    m_clockManager(nullptr),
    m_demuxThread(nullptr),
    m_videoDecodeThread(nullptr),
    m_videoRenderthread(nullptr),
    m_audioDecodeThread(nullptr),
    m_audioRenderThread(nullptr),
    m_controlThread(nullptr)
{
    cout << "MediaPlayer: Initializing..." << endl;

    // 构造函数的职责是保证：要么成功创建一个完整的对象，要么抛出异常并清理所有已分配的资源。
    // 使用一个总的 try-catch 块来捕获任何初始化阶段的失败。
    try {
        init_components(filepath);
        cout << "MediaPlayer: Initialized successfully. All threads started." << endl;
    }
    catch (const std::exception& e) {
        // 如果 init_components 的任何一步抛出异常，都会进入这里。
        // 此时，对象构造失败，需要确保所有已启动的、非 RAII 管理的资源（主要是线程）被正确停止。
        cerr << "MediaPlayer: CRITICAL: Constructor failed: " << e.what() << endl;
        cleanup();
        throw; // 重新抛出异常，通知调用者(main)构造失败。
    }
}

// 初始化流程总调度
void MediaPlayer::init_components(const std::string& filepath) {
    cout << "MediaPlayer: Initializing components..." << endl;

    // 步骤 0: 初始化 与流信息无关的组件
    // 这些操作如果失败 (如 bad_alloc)，会直接抛出异常。
    // PacketQueue 的创建在 init_demuxer_and_decoders()
    const int MAX_VIDEO_FRAMES = 5;
    const int MAX_AUDIO_FRAMES = 10;

    m_videoFrameQueue = std::make_unique<FrameQueue>(MAX_VIDEO_FRAMES);
    m_audioFrameQueue = std::make_unique<FrameQueue>(MAX_AUDIO_FRAMES);
    m_clockManager = std::make_unique<ClockManager>();
    
    cout << "MediaPlayer: Frame queues and clock manager created." << endl;

    // 步骤 1: 初始化所有 FFmpeg 相关资源
    init_ffmpeg_resources(filepath);

    // 确定流信息后，初始化时钟管理器
    if (m_clockManager) {
        bool has_audio = (audioStreamIndex >= 0);
        bool has_video = (videoStreamIndex >= 0);
        m_clockManager->init(has_audio, has_video);
    }

    // 步骤 2: 初始化所有 SDL 相关资源 (渲染器)
    init_sdl_video_renderer();
    init_sdl_audio_renderer();

    // 步骤 3: 所有资源准备就绪，最后启动工作线程
    start_threads();
}

// 封装 FFmpeg 资源的初始化
void MediaPlayer::init_ffmpeg_resources(const std::string& filepath) {
    cout << "MediaPlayer: Initializing FFmpeg resources..." << endl;

    // 为解码器和渲染器分配裸指针包装的 AVFrame/AVPacket
    // 这些资源需要手动管理生命周期，在析构或异常处理中释放
    m_decodingVideoPacket = av_packet_alloc();
    if (!m_decodingVideoPacket) throw std::runtime_error("FFmpeg Init Error: Could not allocate video decoding packet.");

    m_decodingAudioPacket = av_packet_alloc();
    if (!m_decodingAudioPacket) throw std::runtime_error("FFmpeg Init Error: Could not allocate audio decoding packet.");

    m_renderingVideoFrame = av_frame_alloc();
    if (!m_renderingVideoFrame) throw std::runtime_error("FFmpeg Init Error: Could not allocate video rendering frame.");

    m_renderingAudioFrame = av_frame_alloc();
    if (!m_renderingAudioFrame) throw std::runtime_error("FFmpeg Init Error: Could not allocate audio rendering frame");

    // 创建解码器实例 (此时只是空壳)
    m_videoDecoder = std::make_unique<FFmpegVideoDecoder>();
    m_audioDecoder = std::make_unique<FFmpegAudioDecoder>();

    // 调用集成的解复用器和解码器初始化函数
    if (init_demuxer_and_decoders(filepath) != 0) {
        // 不在这里清理，直接抛出异常，让主catch块处理
        throw std::runtime_error("FFmpeg Init Error: Demuxer/Decoder initialization failed.");
    }
    cout << "MediaPlayer: FFmpeg resources initialized successfully." << endl;
}

void MediaPlayer::init_sdl_video_renderer() {
    cout << "MediaPlayer: Initializing SDL video renderer..." << endl;

    // 总是创建 SDLVideoRenderer 实例
    auto sdl_renderer = std::make_unique<SDLVideoRenderer>();

    // 如果有视频流，则进行完整初始化
    if (videoStreamIndex >= 0) {
        cout << "MediaPlayer: Video stream found. Initializing full video renderer." << endl;

        // 从已初始化的解码器获取视频尺寸
        int video_width = m_videoDecoder->getWidth();
        int video_height = m_videoDecoder->getHeight();
        if (video_width <= 0 || video_height <= 0) {
            throw std::runtime_error("SDL Init Error: Video decoder did not provide valid dimensions.");
        }

        if (!sdl_renderer->init("SDLplayerCore (Video)", video_width, video_height,
                                m_videoDecoder->getPixelFormat(), m_clockManager.get())) {
            throw std::runtime_error("SDL Init Error: Failed to initialize SDL Video Renderer.");
        }

        // 设置同步所需的时钟参数
        AVStream* video_stream = m_demuxer->getFormatContext()->streams[videoStreamIndex];
        if (video_stream) {
            sdl_renderer->setSyncParameters(video_stream->time_base, av_q2d(video_stream->avg_frame_rate));
        }
    }
    // 如果没有视频流，但有音频流，则进行纯音频模式的初始化
    else if (audioStreamIndex >= 0) {
        cout << "MediaPlayer: No video stream. Initializing in audio-only mode." << endl;
        // 使用默认尺寸创建一个用于交互的窗口
        if (!sdl_renderer->init("SDLplayerCore (Audio)", 640, 480, AV_PIX_FMT_NONE, m_clockManager.get())) {
            throw std::runtime_error("SDL Init Error: Failed to initialize audio-only window.");
        }
    }
    // 如果视频和音频流都没有，则不创建渲染器
    else {
        cout << "MediaPlayer: No video or audio streams found. Skipping video renderer initialization." << endl;
        return; // 在这种情况下，m_videoRenderer 将保持 nullptr
    }

    // 初始化成功，将准备好的、但尚未初始化的渲染器移交给成员变量
    m_videoRenderer = std::move(sdl_renderer);
    cout << "MediaPlayer: SDL video renderer component initialized successfully." << endl;
}

void MediaPlayer::init_sdl_audio_renderer() {
    if (audioStreamIndex < 0) {
        cout << "MediaPlayer: No audio stream found. Skipping audio renderer initialization." << endl;
        return;
    }
    cout << "MediaPlayer: Initializing SDL Audio Renderer..." << endl;

    m_audioRenderer = std::make_unique<SDLAudioRenderer>();

    // 从解码器获取音频参数
    int sampleRate = m_audioDecoder->getSampleRate();
    int channels = m_audioDecoder->getChannels();
    AVSampleFormat sampleFmt = m_audioDecoder->getSampleFormat();
    AVRational timeBase = m_audioDecoder->getTimeBase();

    if (!m_audioRenderer->init(sampleRate, channels, sampleFmt, timeBase, m_clockManager.get())) {
        throw std::runtime_error("Failed to initialize SDLAudioRenderer");
    }

    cout << "MediaPlayer: SDL Audio Renderer initialized." << endl;
}

// 封装线程的启动
void MediaPlayer::start_threads() {
    cout << "MediaPlayer: Starting worker threads..." << endl;

    // 启动解复用线程
    m_demuxThread = SDL_CreateThread(demux_thread_entry, "DemuxThread", this);
    if (!m_demuxThread) {
        throw std::runtime_error("Thread Error: Could not create demux thread.");
    }
    // 启动视频解码和渲染线程
    if (videoStreamIndex >= 0) {
        m_videoDecodeThread = SDL_CreateThread(video_decode_thread_entry, "VideoDecodeThread", this);
        if (!m_videoDecodeThread) {
            throw std::runtime_error("Thread Error: Could not create video decode thread.");
        }
        m_videoRenderthread = SDL_CreateThread(video_render_thread_entry, "VideoRenderThread", this);
        if (!m_videoRenderthread) {
            throw std::runtime_error("Thread Error: Could not create video render thread.");
        }
    }
    // 启动音频解码和渲染线程
    if (audioStreamIndex >= 0) {
        m_audioDecodeThread = SDL_CreateThread(audio_decode_thread_entry, "AudioDecodeThread", this);
        if (!m_audioDecodeThread) {
            throw std::runtime_error("Thread Error: Could not create audio decode thread.");
        }
        m_audioRenderThread = SDL_CreateThread(audio_render_thread_entry, "AudioRenderThread", this);
        if (!m_audioRenderThread) {
            throw std::runtime_error("Thread Error: Failed to create audio render thread.");
        }
    }
    // 启动总控制子线程
    m_controlThread = SDL_CreateThread(control_thread_entry, "ControlThread", this);
    if (!m_controlThread) {
        throw std::runtime_error("Thread Error: Failed to create control thread.");
    }

    cout << "MediaPlayer: Worker threads started." << endl;
}

MediaPlayer::~MediaPlayer() {
    cout << "MediaPlayer: Destructing..." << endl;
    cleanup();
    cout << "MediaPlayer: Destruction complete." << endl;
}

int MediaPlayer::init_demuxer_and_decoders(const string& filepath) {
    cout << "MediaPlayer: Initializing Demuxer and Decoders for: " << filepath << endl;

    // 路径验证
    if (filepath.empty()) {
        cerr << "FFmpeg Init Error: Input path/URL is empty." << endl;
        return -1;
    }

    //1、创建并打开解复用器Demuxer
    m_demuxer = std::make_unique<FFmpegDemuxer>();
    if (!m_demuxer->open(filepath.c_str())) {
        cerr << "MediaPlayer Error: Demuxer failed to open input: " << filepath << endl;
        return -1;
    }
    cout << "MediaPlayer: Demuxer opened successfully." << endl;

    // 2、查找音视频流
    videoStreamIndex = m_demuxer->findStream(AVMEDIA_TYPE_VIDEO);
    audioStreamIndex = m_demuxer->findStream(AVMEDIA_TYPE_AUDIO);

    // 检查是否至少有一个可播放的流
    if (videoStreamIndex < 0 && audioStreamIndex < 0) {
        cerr << "MediaPlayer Error: Demuxer didn't find any video or audio streams." << endl;
        return -1; // 致命错误
    }

    // 3. 根据流信息创建 PacketQueue
    if (videoStreamIndex >= 0) {
        AVRational time_base = m_demuxer->getTimeBase(videoStreamIndex);
        if (time_base.den == 0) {
            cerr << "MediaPlayer Warning: Invalid video time_base { " << time_base.num << ", " << time_base.den << " }. Using default PacketQueue settings." << endl;
            m_videoPacketQueue = std::make_unique<PacketQueue>(150, 0);
        }
        else {
            // 设置目标缓冲时长，如10秒
            double target_duration_sec = 10.0;
            int64_t max_duration_ts = static_cast<int64_t>(target_duration_sec / av_q2d(time_base));
            cout << "MediaPlayer: Video PacketQueue configured for " << target_duration_sec << "s buffer (" << max_duration_ts << " in time_base units)." << endl;
            m_videoPacketQueue = std::make_unique<PacketQueue>(150, max_duration_ts);
        }
    }

    if (audioStreamIndex >= 0) {
        AVRational time_base = m_demuxer->getTimeBase(audioStreamIndex);
        if (time_base.den == 0) {
            cerr << "MediaPlayer Warning: Invalid audio time_base { " << time_base.num << ", " << time_base.den << " }. Using default PacketQueue settings." << endl;
            m_audioPacketQueue = std::make_unique<PacketQueue>(150, 0);
        }
        else {
            // 音频缓冲可以设置得更长一些，如15秒
            double target_duration_sec = 15.0;
            int64_t max_duration_ts = static_cast<int64_t>(target_duration_sec / av_q2d(time_base));
            cout << "MediaPlayer: Audio PacketQueue configured for " << target_duration_sec << "s buffer (" << max_duration_ts << " in time_base units)." << endl;
            m_audioPacketQueue = std::make_unique<PacketQueue>(150, max_duration_ts);
        }
    }

    // 4. 初始化视频解码器 (如果视频流存在)
    if (videoStreamIndex >= 0) {
        cout << "MediaPlayer: Video stream found at index: " << videoStreamIndex << endl;
        AVCodecParameters* pVideoCodecParams = m_demuxer->getCodecParameters(videoStreamIndex);

        // 获取视频流的时间基
        AVRational videoTimeBase = m_demuxer->getTimeBase(videoStreamIndex);

        if (!pVideoCodecParams || !m_videoDecoder->init(pVideoCodecParams, videoTimeBase)) {
            cerr << "MediaPlayer Warning: Failed to initialize video decoder. Ignoring video." << endl;
            videoStreamIndex = -1;
        }
        else {
            cout << "MediaPlayer: Video decoder initialized successfully." << endl;
        }
    }
    else {
        cout << "MediaPlayer: No video stream found." << endl;
    }

    // 5. 初始化音频解码器 (如果音频流存在)
    if (audioStreamIndex >= 0) {
        cout << "MediaPlayer: Audio stream found at index: " << audioStreamIndex << endl;
        AVCodecParameters* pAudioCodecParams = m_demuxer->getCodecParameters(audioStreamIndex);
        AVRational audioTimeBase = m_demuxer->getTimeBase(audioStreamIndex);
        if (!pAudioCodecParams || !m_audioDecoder->init(pAudioCodecParams, audioTimeBase, m_clockManager.get())) {
            cerr << "MediaPlayer Warning: Failed to initialize audio decoder. Ignoring audio." << endl;
            audioStreamIndex = -1;
        }
        else {
            cout << "MediaPlayer: Audio decoder initialized successfully." << endl;
        }
    }
    else {
        cout << "MediaPlayer: No audio stream found." << endl;
    }

    // 6. 再次检查，如果所有流都初始化失败，则报错
    if (videoStreamIndex < 0 && audioStreamIndex < 0) {
        cerr << "MediaPlayer Error: Failed to initialize any valid decoders." << endl;
        return -1;
    }

    cout << "MediaPlayer: FFmpeg demuxer and decoders initialization process finished." << endl;
    return 0;
}

int MediaPlayer::handle_event(const SDL_Event& event) {
    switch (event.type) {
    // 关闭按钮
    case SDL_QUIT:
    case FF_QUIT_EVENT: // 响应自定义的退出事件
        cout << "MediaPlayer: Quit event received, requesting quit." << endl;
        m_quit = true;
        break;

    case SDL_KEYDOWN:
        // ESC退出
        if (event.key.keysym.sym == SDLK_ESCAPE) {
            cout << "MediaPlayer: Escape key pressed, requesting quit." << endl;
            m_quit = true;
        }
        // 空格键暂停
        if (event.key.keysym.sym == SDLK_SPACE) {
            std::unique_lock<std::mutex> lock(m_state_mutex);
            PlayerState current_state = m_playerState.load();

            if (current_state == PlayerState::PAUSED) {
                // 从暂停恢复到播放
                cout << "MediaPlayer: Resuming from PAUSED state." << endl;

                // 1. 执行重同步操作
                resync_after_pause();

                // 2. 切换到缓冲状态，让 control_thread 接管后续流程
                //    此时时钟在 resync_after_pause() 中已被重置并暂停
                m_playerState.store(PlayerState::BUFFERING);
                cout << "MediaPlayer: Switched to BUFFERING state to refill buffers after pause." << endl;

                // 3. 唤醒 解复用线程 开始拉流，解码/渲染线程 会因状态不为 PLAYING 而继续等待
                lock.unlock();
                m_state_cond.notify_all();
            }
            else if (current_state == PlayerState::PLAYING || current_state == PlayerState::BUFFERING) {
                // 从播放或缓冲状态切换到暂停
                cout << "MediaPlayer: Switching to PAUSED state from "
                    << (current_state == PlayerState::PLAYING ? "PLAYING" : "BUFFERING") << "." << endl;

                m_clockManager->pause();
                m_playerState.store(PlayerState::PAUSED);
                
                // 切换到PAUSED时，不需要notify_all()。
                // 其他线程在下一次循环检查wait条件时会自动阻塞。
            }
            else {
                // 在 IDLE 或 STOPPED 状态下忽略
                cout << "MediaPlayer: Pause ignored. Current state is "
                    << static_cast<int>(current_state) << endl;
            }
        }
        break;

    case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
            event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {

            int newWidth = event.window.data1;
            int newHeight = event.window.data2;
            cout << "MediaPlayer: Window resized to " << newWidth << "x" << newHeight << endl;

            // 通知渲染器处理窗口大小调整
            if (m_videoRenderer) {
                m_videoRenderer->onWindowResize(newWidth, newHeight);
                // 调整大小后，立即用最后一帧刷新一次
                m_videoRenderer->refresh();
            }
        }
        // 窗口暴露事件处理
        else if (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
                event.window.event == SDL_WINDOWEVENT_RESTORED ||
                event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
                event.window.event == SDL_WINDOWEVENT_SHOWN) {
            cout << "MediaPlayer: Window event requires refresh, posting request." << endl;
            if (m_videoRenderer) {
                m_videoRenderer->refresh();
            }
        }
        else if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
            cout << "MediaPlayer: Window close event received, requesting quit." << endl;
            m_quit = true;
        }
        break;

    case FF_REFRESH_EVENT:
        // 响应同步线程的通知，在主线程执行渲染
        if (m_videoRenderer) {
            m_videoRenderer->displayFrame();
            frame_cnt++;
        }
        break;

    default:
        break;
    }
    return 1; // 统一返回1，循环由 m_quit 控制
}

int MediaPlayer::runMainLoop() {
    cout << "MediaPlayer: Starting main loop..." << endl;

    SDL_Event event;
    while (!m_quit) {
        // 使用 WaitEvent, 它会被自定义事件唤醒
        SDL_WaitEvent(&event);
        handle_event(event);
    }

    cout << "MediaPlayer: Main loop finished." << endl;
    return 0;
}

void MediaPlayer::resync_after_pause() {
    cout << "MediaPlayer: Resyncing after pause..." << endl;

    // 清空 SDL 音频设备缓冲
    if (m_audioRenderer) {
        m_audioRenderer->flushBuffers();
    }

    // 清空队列
    if (m_videoPacketQueue) m_videoPacketQueue->clear();
    if (m_audioPacketQueue) m_audioPacketQueue->clear();
    if (m_videoFrameQueue) m_videoFrameQueue->clear();
    if (m_audioFrameQueue) m_audioFrameQueue->clear();

    // 3. 区分流类型处理 Seek 和 时钟
    bool isLive = m_demuxer && m_demuxer->isLiveStream();

    if (isLive) {
        // --- 直播流处理 ---
        // 禁止 seek(0)，否则会导致连接断开或协议错误
        cout << "MediaPlayer: Live stream detected, skipping seek(0)." << endl;

        // 将时钟标记为未知，等待渲染线程收到第一帧后重新校准
        if (m_clockManager) m_clockManager->setClockToUnknown();
    }
    else {
        // --- 本地文件处理 ---
        // Seek 到 0 (重播)
        if (m_demuxer) {
            m_demuxer->seek(0);
        }
        // 重置时钟为 0
        if (m_clockManager) m_clockManager->reset();
    }

    // 刷新解码器上下文
    if (m_videoDecoder) m_videoDecoder->flush();
    if (m_audioDecoder) m_audioDecoder->flush();

    m_demuxer_eof = false; // 重置解复用器EOF标志

    cout << "MediaPlayer: Resync complete." << endl;
}

void MediaPlayer::cleanup_ffmpeg_resources() {
    cout << "MediaPlayer: Cleaning up FFmpeg resources..." << endl;

    // 1. 按依赖逆序显式释放FFmpeg组件，各模块的清理通过 unique_ptr 管理的析构函数来处理
    if (m_videoDecoder) {
        m_videoDecoder.reset(); // .reset()会调用析构函数，析构函数会调用close()
        cout << "MediaPlayer: Video decoder cleaned up." << endl;
    }
    if (m_audioDecoder) {
        m_audioDecoder.reset();
        cout << "MediaPlayer: Audio decoder cleaned up." << endl;
    }
    if (m_demuxer) {
        m_demuxer.reset();
        cout << "MediaPlayer: Demuxer cleaned up." << endl;
    }

    // 2. 清理手动分配的FFmpeg裸指针成员
    if (m_decodingVideoPacket) {
        // av_frame_free 会自动执行解引用 unref 操作
        av_packet_free(&m_decodingVideoPacket);
    }
    if (m_renderingVideoFrame) {
        av_frame_free(&m_renderingVideoFrame);
    }
    if (m_decodingAudioPacket) {
        av_packet_free(&m_decodingAudioPacket);
    }
    if (m_renderingAudioFrame) {
        av_frame_free(&m_renderingAudioFrame);
    }

    cout << "MediaPlayer: FFmpeg resources cleanup finished." << endl;
}

void MediaPlayer::cleanup() {
    cout << "MediaPlayer: Initiating full cleanup..." << endl;

    // 1、发送全局退出信号 (所有线程循环的通用退出条件)
    m_quit.store(true);
    // 唤醒所有可能在等待状态变化的线程，让它们能检查到 m_quit
    m_state_cond.notify_all();

    // --- 第 1 阶段：停止生产者 (Demuxer) ---
    cout << "MediaPlayer: Shutting down producer threads..." << endl;
    if (m_demuxer) {
        // 仅向 demuxer 发送中断信号
        // m_demuxer 为 IDemuxer，需要安全向下转型为 FFmpegDemuxer，以调用其特有的 requestAbort()。
        FFmpegDemuxer* ffmpeg_demuxer = dynamic_cast<FFmpegDemuxer*>(m_demuxer.get());
        if (ffmpeg_demuxer) {
            cout << "MediaPlayer: Requesting demuxer interrupt..." << endl;
            ffmpeg_demuxer->requestAbort(true);
        }
    }
    // 立即等待 Demuxer 线程结束，保证不再产生新的 AVPacket
    // Demuxer 退出前会向 Packet Queues 发送 EOF 信号
    if (m_demuxThread) {
        cout << "MediaPlayer: Waiting for demux thread to finish..." << endl;
        SDL_WaitThread(m_demuxThread, nullptr);
        cout << "MediaPlayer: Demux thread finished." << endl;
    }

    // --- 第 2 阶段：停止中间处理者 (Decoders) ---
    cout << "MediaPlayer: Shutting down decoder threads..." << endl;

    // 在等待 Decoder 线程前，
    // 必须 abort 其输入队列 (PacketQueue) 和输出队列 (FrameQueue)。
    // 确保无论 Decoder 卡在 pop() 还是 push()都能被唤醒。
    if (m_videoPacketQueue) m_videoPacketQueue->abort();
    if (m_audioPacketQueue) m_audioPacketQueue->abort();
    if (m_videoFrameQueue) m_videoFrameQueue->abort();
    if (m_audioFrameQueue) m_audioFrameQueue->abort();

    if (m_videoDecodeThread) {
        cout << "MediaPlayer: Waiting for video decode thread to finish..." << endl;
        SDL_WaitThread(m_videoDecodeThread, nullptr);
        cout << "MediaPlayer: Video decode thread finished." << endl;
    }
    if (m_audioDecodeThread) {
        cout << "MediaPlayer: Waiting for audio decode thread to finish..." << endl;
        SDL_WaitThread(m_audioDecodeThread, nullptr);
        cout << "MediaPlayer: Audio decode thread finished." << endl;
    }
    // 此刻保证不会再有新的 AVFrame 产生。
    // Decoder 退出前会向 Frame Queues 发送 EOF 信号。

    // --- 第 3 阶段：停止消费者 (Renderers) ---
    cout << "MediaPlayer: Shutting down consumer threads..." << endl;
    // 渲染线程会因为 FrameQueue 的 EOF 而自然退出。

    // 推送退出事件，确保主线程的 SDL_WaitEvent 也能退出
    SDL_Event event;
    event.type = FF_QUIT_EVENT;
    SDL_PushEvent(&event);

    if (m_videoRenderthread) {
        cout << "MediaPlayer: Waiting for video render thread to finish..." << endl;
        SDL_WaitThread(m_videoRenderthread, nullptr);
        cout << "MediaPlayer: Video render thread finished." << endl;
    }
    if (m_audioRenderThread) {
        cout << "MediaPlayer: Waiting for audio render thread to finish..." << endl;
        SDL_WaitThread(m_audioRenderThread, nullptr);
        cout << "MediaPlayer: Audio render thread finished." << endl;
    }
    // 退出总控制子线程
    if (m_controlThread) {
        cout << "MediaPlayer: Waiting for control thread to finish..." << endl;
        SDL_WaitThread(m_controlThread, nullptr);
        cout << "MediaPlayer: Control thread finished." << endl;
    }
    cout << "MediaPlayer: All threads have been joined." << endl;
    
    // --- 第 4 阶段：清理资源 ---
    cout << "MediaPlayer: Cleaning up resources..." << endl;

    // 释放SDL渲染器(依赖于FFmpeg信息)
    if (m_audioRenderer) {
        m_audioRenderer.reset();
        cout << "MediaPlayer: Audio Renderer cleaned up." << endl;
    }
    if (m_videoRenderer) {
        m_videoRenderer.reset();
        cout << "MediaPlayer: Video Renderer cleaned up." << endl;
    }

    // 释放FFmpeg核心资源
    cleanup_ffmpeg_resources();

    // 释放队列和时钟
    if (m_videoPacketQueue) {
        m_videoPacketQueue.reset();
        cout << "MediaPlayer: Video packet queue cleaned up." << endl;
    }
    if (m_audioPacketQueue) {
        m_audioPacketQueue.reset();
        cout << "MediaPlayer: Audio packet queue cleaned up." << endl;
    }
    if (m_videoFrameQueue) {
        m_videoFrameQueue.reset();
        cout << "MediaPlayer: Video frame queue cleaned up." << endl;
    }
    if (m_audioFrameQueue) {
        m_audioFrameQueue.reset();
        cout << "MediaPlayer: Audio frame queue cleaned up." << endl;
    }
    if (m_clockManager) {
        m_clockManager.reset();
        cout << "MediaPlayer: Clock manager cleaned up." << endl;
    }

    cout << "MediaPlayer: Full cleanup finished." << endl;
}

// 解复用线程入口和主函数
int MediaPlayer::demux_thread_entry(void* opaque) {
    // 获取 MediaPlayer 实例指针
    return static_cast<MediaPlayer*>(opaque)->demux_thread_func();
}

int MediaPlayer::demux_thread_func() {
    cout << "MediaPlayer: Demux thread started." << endl;
    AVPacket* demux_packet = av_packet_alloc();
    if (!demux_packet) {
        cerr << "MediaPlayer DemuxThread Error: Could not allocate demux_packet." << endl;
        if (m_videoPacketQueue) { m_videoPacketQueue->signal_eof(); } // 将 错误 作为EOF 进行传递
        if (m_audioPacketQueue) { m_audioPacketQueue->signal_eof(); }
        m_quit = true;  // 严重错误，请求主播放器退出
        return -1;
    }

    int read_ret = 0;
    bool isLive = m_demuxer && m_demuxer->isLiveStream();

    while (!m_quit) {
        // 获取当前状态
        PlayerState currentState = m_playerState.load();
        
        if (currentState == PlayerState::PAUSED) {
            if (isLive) {
                // 【直播流-假暂停策略】
                // 为了防止 TCP 断连 和服务器缓冲区溢出，
                // 暂停时必须继续读取数据，但直接丢包。

                read_ret = m_demuxer->readPacket(demux_packet);
                if (read_ret >= 0) {
                    // 读取成功，直接释放引用（丢包）
                    av_packet_unref(demux_packet);
                }
                else {
                    // 读取出错 (例如 EOF 或 网络真的断了)
                    if (read_ret != AVERROR(EAGAIN)) {
                        // 记录错误但根据情况决定是否退出
                        // 这里简单打印警告，如果是严重错误会在后续循环中被捕获或自行决定退出
                        cerr << "Warning: Live stream read error during pause." << endl;
                    }
                }

                // 必须延时！否则这个空循环会占满一个 CPU 核心
                SDL_Delay(10);
                continue; // 跳过后续入队逻辑，直接进入下一次循环
            }
            else {
                // 【本地文件-真暂停策略】
                std::unique_lock<std::mutex> lock(m_state_mutex);
                m_state_cond.wait(lock, [this] {
                    return m_playerState != PlayerState::PAUSED || m_quit;
                    });
            }
        }

        // 如果是因退出而被唤醒，则直接退出循环
        if (m_quit) break;

        read_ret = m_demuxer->readPacket(demux_packet);

        if (read_ret < 0) {
            if (read_ret == AVERROR_EOF) {
                cout << "MediaPlayer DemuxThread: Demuxer reached EOF." << endl;
                m_demuxer_eof = true;
                if (m_videoPacketQueue) { m_videoPacketQueue->signal_eof(); }
                if (m_audioPacketQueue) { m_audioPacketQueue->signal_eof(); }
            }
            else {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, read_ret);
                cerr << "MediaPlayer DemuxThread Error: Demuxer failed to read packet: " << errbuf << endl;
                if (m_videoPacketQueue) { m_videoPacketQueue->signal_eof(); }
                if (m_audioPacketQueue) { m_audioPacketQueue->signal_eof(); }
                m_quit = true; // 严重错误
            }
            break; // 退出解复用循环
        }

        if (demux_packet->stream_index == videoStreamIndex) {
            if (m_videoPacketQueue) {
                if (!m_videoPacketQueue->push(demux_packet)) {
                    // Push 推送失败（例如 队列已满且配置为不阻塞/丢弃 或 已发出 EOF 信号）
                    // PacketQueue 的 push 在队列已满时记录错误并丢弃数据包
                    // 该数据包将在下方取消引用，因此这里不用额外处理
                }
            }
        }
        else if (audioStreamIndex >= 0 && demux_packet->stream_index == audioStreamIndex) {
            if (m_audioPacketQueue) {
                if (!m_audioPacketQueue->push(demux_packet)) {
                    // 推送失败，包被丢弃。此处无需额外操作
                }
            }
        }
        // else {
            // 来自其他流的数据包，暂时忽略
        // }

        // PacketQueue::push 会调用 av_packet_ref，因此这里解复用线程读取的原始包需要 unref
        av_packet_unref(demux_packet);
    }

    av_packet_free(&demux_packet); // 释放本地包

    // 确保即使循环因 m_quit 退出，EOF也会发送
    if (m_videoPacketQueue && !m_videoPacketQueue->is_eof()) {
        cout << "MediaPlayer DemuxThread: Signaling EOF on video packet queue as thread exits." << endl;
        m_videoPacketQueue->signal_eof();
    }
    if (m_audioPacketQueue && !m_audioPacketQueue->is_eof()) {
        cout << "MediaPlayer DemuxThread: Signaling EOF on audio packet queue as thread exits." << endl;
        m_audioPacketQueue->signal_eof();
    }

    return 0;
}

// 视频解码线程入口和主函数
int MediaPlayer::video_decode_thread_entry(void* opaque) {
    return static_cast<MediaPlayer*>(opaque)->video_decode_func();
}

// 解码线程在 BUFFERING 和 PAUSED 状态下都应暂停
int MediaPlayer::video_decode_func() {
    cout << "MediaPlayer: Video decode thread started." << endl;
    if (!m_videoDecoder || !m_videoPacketQueue || !m_videoFrameQueue) {
        cerr << "MediaPlayer VideoDecodeThread Error: Decoder or queues not initialized." << endl;
        if (m_videoFrameQueue) m_videoFrameQueue->signal_eof();
        return -1;
    }

    AVFrame* decoded_frame = nullptr;

    while (!m_quit) {
        // 状态等待逻辑
        {
            std::unique_lock<std::mutex> lock(m_state_mutex);
            m_state_cond.wait(lock, [this] { 
                return m_playerState == PlayerState::PLAYING || m_quit;
                });
        }
        if (m_quit) break;

        if (!m_videoPacketQueue->pop(m_decodingVideoPacket, -1)) {
            // 检查 EOF，处理正常的流结束冲洗
            if (m_videoPacketQueue->is_eof()) {
                cout << "MediaPlayer VideoDecodeThread: Packet queue EOF, starting to flush decoder." << endl;
                int flush_ret = m_videoDecoder->decode(nullptr, &decoded_frame); // 发送 nullptr 来冲洗
                while (flush_ret == 0) {
                    if (decoded_frame) {
                        if (!m_videoFrameQueue->push(decoded_frame)) {
                            if (m_quit.load()) {
                                cout << "MediaPlayer VideoDecodeThread: Discarding flushed frame as shutdown is in progress." << endl;
                            }
                            else {
                                cerr << "MediaPlayer VideoDecodeThread: Failed to push flushed frame to frame queue." << endl;
                            }
                            // 无论如何都要释放 frame
                            av_frame_free(&decoded_frame);
                            // 下游队列已满/中止，无法继续推送，应中断冲洗
                            break;
                        }
                        av_frame_free(&decoded_frame);
                    }
                    flush_ret = m_videoDecoder->decode(nullptr, &decoded_frame); // 尝试获取更多
                }
                if (flush_ret == AVERROR_EOF) {
                    cout << "MediaPlayer VideoDecodeThread: Video decoder fully flushed." << endl;
                }
                else if (flush_ret != AVERROR(EAGAIN)) {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE];
                    av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, flush_ret);
                    cerr << "MediaPlayer VideoDecodeThread: Error flushing decoder: " << errbuf << endl;
                }
                m_videoFrameQueue->signal_eof();
                break;
            }
            else {
                // abort()，直接退出
                cout << "MediaPlayer VideoDecodeThread: Packet queue aborted, exiting loop." << endl;
            }
            break; // 退出主循环
        }

        int decode_ret = m_videoDecoder->decode(m_decodingVideoPacket, &decoded_frame);
        av_packet_unref(m_decodingVideoPacket);

        if (decode_ret == 0 && decoded_frame) {
            if (!m_videoFrameQueue->push(decoded_frame)) {
                // 检查是不是因为程序正在退出
                if (m_quit.load()) {
                    cout << "MediaPlayer VideoDecodeThread: Discarding frame as shutdown is in progress." << endl;
                }
                else {
                    cerr << "MediaPlayer VideoDecodeThread: Failed to push decoded frame to frame queue." << endl;
                }
            }
            av_frame_free(&decoded_frame);
        }
        else if (decode_ret == AVERROR(EAGAIN)) {
            // 继续循环，尝试发送下一个包或接收帧
        }
        else if (decode_ret == AVERROR_EOF) {
            cout << "MediaPlayer VideoDecodeThread: Decoder signaled EOF during decoding." << endl;
            m_videoFrameQueue->signal_eof();
            break;
        }
        else {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, decode_ret);
            cerr << "MediaPlayer VideoDecodeThread: Error decoding packet: " << errbuf << endl;
            m_videoFrameQueue->signal_eof();
            m_quit = true;
            break;
        }
    }

    if (m_videoFrameQueue && !m_videoFrameQueue->is_eof()) {
        cout << "MediaPlayer VideoDecodeThread: Signaling EOF on video frame queue as thread exits." << endl;
        m_videoFrameQueue->signal_eof();
    }

    return 0;
}

// 音频解码线程入口和主函数
int MediaPlayer::audio_decode_thread_entry(void* opaque) {
    return static_cast<MediaPlayer*>(opaque)->audio_decode_func();
}

int MediaPlayer::audio_decode_func() {
    cout << "MediaPlayer: Audio decode thread started." << endl;
    if (!m_audioDecoder || !m_audioPacketQueue || !m_audioFrameQueue) {
        cerr << "MediaPlayer AudioDecodeThread Error: Decoder or queues not initialized." << endl;
        if (m_audioFrameQueue) m_audioFrameQueue->signal_eof();
        return -1;
    }

    AVFrame* decoded_frame = nullptr;

    while (!m_quit) {
        // 状态等待逻辑
        {
            std::unique_lock<std::mutex> lock(m_state_mutex);
            m_state_cond.wait(lock, [this] {
                return m_playerState == PlayerState::PLAYING || m_quit;
                });
        }
        if (m_quit) break;

        // 1. 从音频包队列中取出一个包
        if (!m_audioPacketQueue->pop(m_decodingAudioPacket, -1)) {
            // 检查 EOF
            if (m_audioPacketQueue->is_eof()) {
                cout << "MediaPlayer AudioDecodeThread: Packet queue EOF, starting to flush decoder." << endl;

                // 发送 nullptr 来刷新解码器
                int flush_ret = m_audioDecoder->decode(nullptr, &decoded_frame);
                while (flush_ret == 0) { // 持续获取帧直到解码器无更多输出
                    if (decoded_frame) {
                        if (!m_audioFrameQueue->push(decoded_frame)) {
                            if (m_quit.load()) {
                                cout << "MediaPlayer AudioDecodeThread: Discarding flushed frame as shutdown is in progress." << endl;
                            }
                            else {
                                cerr << "MediaPlayer AudioDecodeThread: Failed to push flushed frame to frame queue." << endl;
                            }
                            // 始终释放 frame
                            av_frame_free(&decoded_frame);
                            // 下游队列已满/中止，无法继续推送，中断冲洗
                            break;
                        }
                        av_frame_free(&decoded_frame);
                    }
                    // 尝试获取下一个冲洗帧
                    flush_ret = m_audioDecoder->decode(nullptr, &decoded_frame);
                }
                if (flush_ret == AVERROR_EOF) {
                    cout << "MediaPlayer AudioDecodeThread: Audio decoder fully flushed." << endl;
                }
                else if (flush_ret != AVERROR(EAGAIN)) {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE];
                    av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, flush_ret);
                    cerr << "MediaPlayer AudioDecodeThread: Error flushing audio decoder: " << errbuf << endl;
                }

                m_audioFrameQueue->signal_eof(); // 向音频帧队列发送EOF信号
                break; // 退出循环
            }
            else {
                cout << "MediaPlayer AudioDecodeThread: Packet queue aborted, exiting loop." << endl;
            }
            break; // 退出循环
        }

        // 2. 解码数据包
        int decode_ret = m_audioDecoder->decode(m_decodingAudioPacket, &decoded_frame);
        av_packet_unref(m_decodingAudioPacket); // 解码后不再需要此数据包

        if (decode_ret == 0 && decoded_frame) {
            if (!m_audioFrameQueue->push(decoded_frame)) {
                // 检查是不是因为程序正在退出
                if (m_quit.load()) {
                    cout << "MediaPlayer AudioDecodeThread: Discarding frame as shutdown is in progress." << endl;
                }
                else {
                    cerr << "MediaPlayer AudioDecodeThread: Failed to push decoded frame to frame queue." << endl;
                }
            }
            av_frame_free(&decoded_frame);
        }
        else if (decode_ret == AVERROR(EAGAIN)) {
            // 解码器需要更多输入，继续循环以获取下一个包
        }
        else if (decode_ret == AVERROR_EOF) {
            cout << "MediaPlayer AudioDecodeThread: Decoder signaled EOF during decoding." << endl;
            m_audioFrameQueue->signal_eof();
            break;
        }
        else {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, decode_ret);
            cerr << "MediaPlayer AudioDecodeThread: Error decoding audio packet: " << errbuf << endl;
            // 发生严重错误，发送EOF信号并退出
            m_audioFrameQueue->signal_eof();
            m_quit = true;
            break;
        }
    }

    if (m_audioFrameQueue && !m_audioFrameQueue->is_eof()) {
        cout << "MediaPlayer AudioDecodeThread: Signaling EOF on audio frame queue as thread exits." << endl;
        m_audioFrameQueue->signal_eof();
    }

    return 0;
}

// 视频渲染线程入口和主函数
int MediaPlayer::video_render_thread_entry(void* opaque) {
    return static_cast<MediaPlayer*>(opaque)->video_render_func();
}

int MediaPlayer::video_render_func() {
    cout << "MediaPlayer: VideoRenderThread started." << endl;
    if (!m_renderingVideoFrame) {
        cerr << "MediaPlayer VideoRenderThread Error: m_renderingVideoFrame is null." << endl;
        return -1;
    }

    while (!m_quit) {
        // 状态等待逻辑
        {
            std::unique_lock<std::mutex> lock(m_state_mutex);
            m_state_cond.wait(lock, [this] { 
                return m_playerState == PlayerState::PLAYING || m_quit;
                });
        }
        if (m_quit) break;

        // 尝试从队列获取新帧
        bool got_new_frame = m_videoFrameQueue->pop(m_renderingVideoFrame, -1);

        if (got_new_frame) {
            // 计算需要延迟多久
            double delay = m_videoRenderer->calculateSyncDelay(m_renderingVideoFrame);
            // 收到丢帧信号：释放当前帧，并立即开始下一次循环以获取新帧
            // 为了避免浮点数比较的潜在问题，使用 < 0.0 来判断帧是否迟到
            if (delay < 0.0) {
                cout << "MediaPlayer VideoRenderThread: Dropping a frame to catch up." << endl;
                av_frame_unref(m_renderingVideoFrame);
                continue; // 直接跳到 while 循环的下一次迭代
            }

            if (delay > 0.0) {
                SDL_Delay(static_cast<Uint32>(delay * 1000.0));
            }

            // 如果已经因为 m_quit = true 被唤醒，检查一下再发事件
            if (m_quit) break;

            // 准备渲染数据 (sws_scale等)，这是一个CPU密集型操作，适合放在该工作线程
            // 只把数据准备好，但不呈现
            if (!m_videoRenderer->prepareFrameForDisplay(m_renderingVideoFrame)) {
                cerr << "MediaPlayer VideoRenderThread: prepareFrameForDisplay failed." << endl;
                // 不一定是致命错误，可以继续
            }

            // 发送刷新事件通知主线程
            SDL_Event event;
            event.type = FF_REFRESH_EVENT;
            SDL_PushEvent(&event);

            av_frame_unref(m_renderingVideoFrame);
        }
        else {
            cout << "MediaPlayer VideoRenderThread: pop() returned false, exiting loop." << endl;
            break;
        }
    }

    // 线程退出前，发送一个最后的退出信号，确保主循环能被唤醒并退出
    SDL_Event event;
    event.type = FF_QUIT_EVENT;
    SDL_PushEvent(&event);

    return 0;
}

// 音频渲染线程入口和主函数
int MediaPlayer::audio_render_thread_entry(void* opaque) {
    return static_cast<MediaPlayer*>(opaque)->audio_render_func();
}

int MediaPlayer::audio_render_func() {
    cout << "MediaPlayer: Audio render thread started." << endl;
    if (!m_renderingAudioFrame) {
        cerr << "MediaPlayer AudioRenderThread Error: m_renderingAudioFrame is null." << endl;
        return -1;
    }

    while (!m_quit) {
        // 状态等待逻辑
        {
            std::unique_lock<std::mutex> lock(m_state_mutex);
            m_state_cond.wait(lock, [this] { 
                return m_playerState == PlayerState::PLAYING || m_quit;
                });
        }
        if (m_quit) break;

        // 从音频帧队列中取出一帧
        if (!m_audioFrameQueue->pop(m_renderingAudioFrame, -1)) {
            cout << "MediaPlayer AudioRenderThread: pop() returned false, exiting loop." << endl;
            break;
        }

        // 调用渲染器来处理这一帧
        if (m_audioRenderer && !m_audioRenderer->renderFrame(m_renderingAudioFrame, m_quit)) {
            // 如果 renderFrame 因为退出请求或其他错误而返回 false，则准备退出线程
            if (!m_quit) {
                cerr << "MediaPlayer AudioRenderThread: renderFrame failed." << endl;
                m_quit = true; // 渲染出错，终止播放
            }
        }

        // 释放对帧数据的引用，以便 m_renderingAudioFrame 可以被重用
        av_frame_unref(m_renderingAudioFrame);
    }

    return 0;
}

// 总控制线程入口和主函数
int MediaPlayer::control_thread_entry(void* opaque) {
    return static_cast<MediaPlayer*>(opaque)->control_thread_func();
}

int MediaPlayer::control_thread_func() {
    cout << "MediaPlayer: Control thread started." << endl;

    AVRational time_base = { 0, 1 };

    // 优先使用视频流的时间基，如果不存在则使用音频流
    if (videoStreamIndex != -1) {
        time_base = m_demuxer->getTimeBase(videoStreamIndex);
    }
    else if (audioStreamIndex != -1) {
        time_base = m_demuxer->getTimeBase(audioStreamIndex);
    }

    // 校验获取到的time_base是否有效
    if (time_base.den == 0) {
        cerr << "MediaPlayer ControlThread Error: Could not determine a valid time_base for buffering." << endl;
        return -1;
    }

    while (!m_quit) {
        // 每 150ms 检查一次状态
        SDL_Delay(150);

        // 获取当前主PacketQueue的缓冲时长（秒）
        // 优先基于视频队列计算，若无视频则基于音频队列
        double current_buffer_sec = 0.0;
        if (videoStreamIndex != -1 && m_videoPacketQueue) {
            int64_t duration_ts = m_videoPacketQueue->getTotalDuration();
            current_buffer_sec = duration_ts * av_q2d(time_base);
        }
        else if (audioStreamIndex != -1 && m_audioPacketQueue) {
            int64_t duration_ts = m_audioPacketQueue->getTotalDuration();
            AVRational audio_time_base = m_demuxer->getTimeBase(audioStreamIndex);
            if (audio_time_base.den > 0) {
                current_buffer_sec = duration_ts * av_q2d(audio_time_base);
            }
        }

        PlayerState current_state = m_playerState.load();

        // --- 决策逻辑 ---
        switch (current_state) {
        case PlayerState::BUFFERING:
        {
            // 检查是否 解复用已结束 且 包队列已空（适用于文件太短无法达到缓冲阈值的情况）
            bool demux_finished_and_queues_empty = m_demuxer_eof.load() &&
                (!m_videoPacketQueue || m_videoPacketQueue->size() == 0) &&
                (!m_audioPacketQueue || m_audioPacketQueue->size() == 0);

            // 如果缓冲时长达到“高水位线”，或者流已结束，则切换到播放状态
            if (current_buffer_sec >= PLAYOUT_THRESHOLD_SEC || demux_finished_and_queues_empty)
            {
                cout << "MediaPlayer ControlThread: Buffering complete. Switching to PLAYING." << endl;

                // 1. 恢复时钟。此时是播放开始的精确时刻。
                if (m_clockManager) {
                    m_clockManager->resume();
                }

                // 2. 切换到播放状态
                m_playerState.store(PlayerState::PLAYING);

                // 3. 唤醒解码和渲染线程开始工作
                m_state_cond.notify_all();
            }
            break;
        }
        case PlayerState::PLAYING:
            // 如果缓冲时长低于“低水位线”，并且数据流还未结束，则切换回缓冲状态
            if (current_buffer_sec < REBUFFER_THRESHOLD_SEC && !m_demuxer_eof.load())
            {
                cout << "MediaPlayer ControlThread: Buffer running low. Switching to BUFFERING." << endl;
                m_playerState.store(PlayerState::BUFFERING);
                // 不需要 notify，其他线程在下一次循环中会检测到新状态并自动等待
            }
            break;

        case PlayerState::PAUSED:
        case PlayerState::IDLE:
        case PlayerState::STOPPED:
            // 在这些状态下，控制线程不进行任何干预
            break;
        }
    }
    return 0;
}
