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
    m_playerState(PlayerState::IDLE),
    m_demuxer_eof(false),
    videoStreamIndex(-1),
    audioStreamIndex(-1),
    frame_cnt(0),
    m_seek_serial(0),
    m_debugStats(nullptr),
    m_wait_for_keyframe(true),
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

    // 构造函数保证：要么成功创建一个完整的对象，要么抛出异常并清理所有已分配的资源。
    // 使用一个总 try-catch 块来捕获任何初始化阶段的失败。
    try {
        // 创建调试数据对象
        m_debugStats = std::make_shared<PlayerDebugStats>();
        // 播放器缓冲
        setPlayerState(PlayerState::BUFFERING);
        // 启动组件和线程
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
    // 注入调试信息 stats
    if (m_videoRenderer) {
        m_videoRenderer->setDebugStats(m_debugStats);
    }
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

    // 创建并打开解复用器
    m_demuxer = std::make_unique<FFmpegDemuxer>();
    if (!m_demuxer->open(filepath.c_str())) {
        cerr << "MediaPlayer Error: Demuxer failed to open input: " << filepath << endl;
        return -1;
    }
    cout << "MediaPlayer: Demuxer opened successfully." << endl;

    // --- 获取流类型 ---
    // 区分直播和点播，以决定队列的行为策略
    bool isLive = m_demuxer->isLiveStream();
    bool block_on_full = !isLive; // 本地文件(非Live)需要阻塞，直播需要丢包
    cout << "MediaPlayer: Stream Mode: " << (isLive ? "LIVE (Drop on full)" : "LOCAL/VOD (Block on full)") << endl;

    // 查找流
    videoStreamIndex = m_demuxer->findStream(AVMEDIA_TYPE_VIDEO);
    audioStreamIndex = m_demuxer->findStream(AVMEDIA_TYPE_AUDIO);

    // 检查是否至少有一个可播放的流
    if (videoStreamIndex < 0 && audioStreamIndex < 0) {
        cerr << "MediaPlayer Error: Demuxer didn't find any video or audio streams." << endl;
        return -1;
    }

    // 初始化 PacketQueue
    if (videoStreamIndex >= 0) {
        AVRational time_base = m_demuxer->getTimeBase(videoStreamIndex);
        // 如果无法获取时间基，或者直播流，给一个保守的默认值
        if (time_base.den == 0) {
            // 默认行为
            cerr << "MediaPlayer Warning: Invalid video time_base { " << time_base.num << ", " << time_base.den << " }. Using default PacketQueue settings." << endl;
            m_videoPacketQueue = std::make_unique<PacketQueue>(150, 0, block_on_full);
        }
        else {
            // 本地文件给大一点的缓冲(如10秒)，直播流给小一点(如2秒)
            double target_duration_sec = isLive ? 2.0 : 10.0;
            int64_t max_duration_ts = static_cast<int64_t>(target_duration_sec / av_q2d(time_base));

            cout << "MediaPlayer: Video PacketQueue configured for " << target_duration_sec
                << "s buffer. Strategy: " << (block_on_full ? "BLOCK" : "DROP") << endl;

            m_videoPacketQueue = std::make_unique<PacketQueue>(150, max_duration_ts, block_on_full);
        }
    }

    if (audioStreamIndex >= 0) {
        AVRational time_base = m_demuxer->getTimeBase(audioStreamIndex);
        if (time_base.den == 0) {
            cerr << "MediaPlayer Warning: Invalid audio time_base { " << time_base.num << ", " << time_base.den << " }. Using default PacketQueue settings." << endl;
            m_audioPacketQueue = std::make_unique<PacketQueue>(200, 0, block_on_full);
        }
        else {
            // 音频缓冲可以设置得更长一些
            double target_duration_sec = isLive ? 3.0 : 15.0;
            int64_t max_duration_ts = static_cast<int64_t>(target_duration_sec / av_q2d(time_base));

            cout << "MediaPlayer: Audio PacketQueue configured for " << target_duration_sec
                << "s buffer. Strategy: " << (block_on_full ? "BLOCK" : "DROP") << endl;

            m_audioPacketQueue = std::make_unique<PacketQueue>(200, max_duration_ts, block_on_full);
        }
    }

    // 初始化视频解码器 (如果视频流存在)
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

    // 初始化音频解码器 (如果音频流存在)
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

    // 再次检查，如果所有流都初始化失败，则报错
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
        // 空格键暂停/恢复
        if (event.key.keysym.sym == SDLK_SPACE) {
            std::unique_lock<std::mutex> lock(m_state_mutex);
            PlayerState current_state = m_playerState.load();
            bool isLive = m_demuxer && m_demuxer->isLiveStream();

            if (current_state == PlayerState::PAUSED) {
                // --- 恢复播放 ---
                cout << "MediaPlayer: Resuming from PAUSED..." << endl;
                
                if (isLive) {
                    // 【直播流 - 重型恢复】
                    // 必须消除暂停期间积累的巨大延迟
                    cout << "MediaPlayer: Heavy Resync for LIVE mode." << endl;

                    // 清空旧数据
                    resync_after_pause();
                    // 必须重新缓冲以填满 jitter buffer (PacketQueue)
                    setPlayerState(PlayerState::BUFFERING);
                    cout << "MediaPlayer: Switched to BUFFERING state to refill buffers after pause." << endl;
                }
                else {
                    // 【本地文件 - 轻量级恢复】
                    // 队列里有数据，解码器状态完好，直接继续
                    cout << "MediaPlayer: Lightweight Resume for LOCAL mode." << endl;

                    // 恢复时钟 (补回暂停流逝的时间)
                    if (m_clockManager) m_clockManager->resume();

                    // 直接切回播放状态 (消费者线程会立即读到队列里的现存数据)
                    setPlayerState(PlayerState::PLAYING);
                }

                // 唤醒所有等待线程
                lock.unlock();
                m_state_cond.notify_all();
            }
            else if (current_state == PlayerState::PLAYING || current_state == PlayerState::BUFFERING) {
                // --- 暂停播放 ---
                cout << "MediaPlayer: Pausing..." << endl;

                // 暂停时钟
                if (m_clockManager) {
                    m_clockManager->pause();
                }
                
                // 切换状态
                setPlayerState(PlayerState::PAUSED);
                
                // 不需要notify，其他线程会在下一次循环检查条件时自动阻塞
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
    cout << "MediaPlayer: Executing Force Resync (Flush queues & state)..." << endl;

    // 序列号自增
    m_seek_serial++;
    cout << "MediaPlayer: Serial updated to " << m_seek_serial.load() << endl;

    // 清空 SDL 音频设备缓冲 (消除几百毫秒的旧声音)
    if (m_audioRenderer) {
        m_audioRenderer->flushBuffers();
    }

    // 清空所有队列
    if (m_videoPacketQueue) m_videoPacketQueue->clear();
    if (m_audioPacketQueue) m_audioPacketQueue->clear();
    if (m_videoFrameQueue) m_videoFrameQueue->clear();
    if (m_audioFrameQueue) m_audioFrameQueue->clear();

    // 刷新解码器 (清空 ffmpeg 内部缓存)
    if (m_videoDecoder) m_videoDecoder->flush();
    if (m_audioDecoder) m_audioDecoder->flush();
    
    // 恢复播放后，必须等待第一个关键帧才开始解码
    m_wait_for_keyframe = true;

    // 特殊处理
    bool isLive = m_demuxer && m_demuxer->isLiveStream();
    if (isLive) {
        // 直播流：时钟置为未知，等待新的一帧到来重新校准同步
        if (m_clockManager) {
            m_clockManager->setClockToUnknown();
        }
    }

    m_demuxer_eof = false; // 重置解复用器EOF标志
    cout << "MediaPlayer: Resync complete. Waiting for Keyframe." << endl;
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

    // 发送全局退出信号 
    m_quit.store(true);
    m_state_cond.notify_all();
    
    // 在等待线程前，必须先打断所有因队列空/满而导致的阻塞（push/pop）
    if (m_videoPacketQueue) m_videoPacketQueue->abort();
    if (m_audioPacketQueue) m_audioPacketQueue->abort();
    if (m_videoFrameQueue) m_videoFrameQueue->abort();
    if (m_audioFrameQueue) m_audioFrameQueue->abort();

    // --- 停止生产者 (Demuxer) ---
    cout << "MediaPlayer: Shutting down producer threads..." << endl;
    
    // 中断 FFmpeg 底层 IO (防止卡在 av_read_frame)
    if (m_demuxer) {
        // 仅向 demuxer 发送中断信号
        // m_demuxer 安全向下转型为 FFmpegDemuxer，以调用其特有的 requestAbort()。
        FFmpegDemuxer* ffmpeg_demuxer = dynamic_cast<FFmpegDemuxer*>(m_demuxer.get());
        if (ffmpeg_demuxer) {
            cout << "MediaPlayer: Requesting demuxer interrupt..." << endl;
            ffmpeg_demuxer->requestAbort(true);
        }
    }

    if (m_demuxThread) {
        cout << "MediaPlayer: Waiting for demux thread to finish..." << endl;
        SDL_WaitThread(m_demuxThread, nullptr);
        cout << "MediaPlayer: Demux thread finished." << endl;
    }

    // --- 停止中间处理者 (Decoders) ---
    cout << "MediaPlayer: Shutting down decoder threads..." << endl;

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

    // --- 停止消费者 (Renderers) ---
    cout << "MediaPlayer: Shutting down consumer threads..." << endl;

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
    
    // --- 清理资源 ---
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

        // 错误处理
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

        // 获取当前最新的序列号
        int current_serial = m_seek_serial.load();

        if (demux_packet->stream_index == videoStreamIndex) {
            if (m_videoPacketQueue) {
                m_videoPacketQueue->push(demux_packet, current_serial);
            }
        }
        else if (audioStreamIndex >= 0 && demux_packet->stream_index == audioStreamIndex) {
            if (m_audioPacketQueue) {
                m_audioPacketQueue->push(demux_packet, current_serial);
            }
        }
        // else {
            // 来自其他流的数据包，暂时忽略
        // }

        // PacketQueue::push 会调用 av_packet_ref，因此这里读取的原始包需要 unref
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

// 辅助函数：判断 H.264 Packet 是否包含 IDR 帧
bool is_idr_frame(AVPacket* pkt) {
    if (!pkt || pkt->size < 5) return false;

    // 对于 H.264，需要遍历 NALU
    // 简单的基本方法：寻找 NALU Header (0x000001 或 0x00000001)
    // 然后检查 NAL Type (后 5 bits)
    uint8_t* data = pkt->data;
    int size = pkt->size;

    for (int i = 0; i < size - 4; i++) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            uint8_t nal_type = data[i + 3] & 0x1F;
            if (nal_type == 5) return true; // 5 是 IDR 帧
        }
        else if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) {
            uint8_t nal_type = data[i + 4] & 0x1F;
            if (nal_type == 5) return true; // 5 是 IDR 帧
        }
    }

    // 如果是 H.265 (HEVC)，IDR 类型通常是 19 或 20
    // 如果流包含多种编码，使用下面妥协的“万能方案”
    return (pkt->flags & AV_PKT_FLAG_KEY);
}

int MediaPlayer::video_decode_func() {
    cout << "MediaPlayer: Video decode thread started." << endl;
    if (!m_videoDecoder || !m_videoPacketQueue || !m_videoFrameQueue || !m_debugStats) {
        cerr << "MediaPlayer VideoDecodeThread Error: Components not initialized." << endl;
        if (m_videoFrameQueue) {
            m_videoFrameQueue->signal_eof();
        }
        if (!m_debugStats) {
            cerr << "FATAL: m_debugStats is null!" << endl;
        }
        return -1;
    }

    AVFrame* decoded_frame = nullptr;
    int pkt_serial = 0; // 包的序列号

    while (!m_quit) {
        // 状态等待逻辑
        {
            std::unique_lock<std::mutex> lock(m_state_mutex);
            m_state_cond.wait(lock, [this] {
                return m_playerState == PlayerState::PLAYING ||
                    m_playerState == PlayerState::BUFFERING ||
                    m_quit;
                });
        }

        if (m_quit) break;

        if (!m_videoPacketQueue->pop(m_decodingVideoPacket, pkt_serial, -1)) {
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
                break;
            }
        }

        // 序列号检查
        if (pkt_serial != m_seek_serial.load()) {
            // 直接丢弃，不送入解码器
            cout << "MediaPlayer VideoDecodeThread: Discarding stale packet (serial mismatch)." << endl;
            av_packet_unref(m_decodingVideoPacket);
            continue; // 直接进行下一次循环
        }
        
        // 关键帧检查
        if (m_wait_for_keyframe) {
            // 检查该包是否包含关键帧IDR的标志
            if (!(m_decodingVideoPacket->flags & AV_PKT_FLAG_KEY) || !is_idr_frame(m_decodingVideoPacket)) {
                // 直接丢弃，防止花屏
                av_packet_unref(m_decodingVideoPacket);
                continue; // 跳过解码，取下个包
            }
            else {
                // 等到了关键帧(IDR)
                cout << "MediaPlayer VideoDecodeThread: REAL IDR Keyframe found! Resuming decode." << endl;
                m_wait_for_keyframe = false;
            }
        }

        int decode_ret = m_videoDecoder->decode(m_decodingVideoPacket, &decoded_frame);
        av_packet_unref(m_decodingVideoPacket);

        if (decode_ret == 0 && decoded_frame) {
            // 统计信息-更新解码帧率
            if (m_debugStats) {
                m_debugStats->decode_fps.tick();
                // 更新视频队列信息
                // AVPacket队列时长单位是 stream->time_base、返回的是 pts 单位，
                // 需要将其转换为毫秒。需要获取 time_base。
                if (m_videoDecoder) {
                    AVRational tb = m_videoDecoder->getTimeBase();
                    int64_t dur_pts = m_videoPacketQueue->getTotalDuration();
                    double dur_sec = dur_pts * av_q2d(tb);
                    m_debugStats->vq_duration_ms = static_cast<long long>(dur_sec * 1000);
                    m_debugStats->vq_size = static_cast<int>(m_videoPacketQueue->size());
                }
            }

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
    int pkt_serial = 0;

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
        if (!m_audioPacketQueue->pop(m_decodingAudioPacket, pkt_serial, -1)) {
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

        // 序列号检查
        if (pkt_serial != m_seek_serial.load()) {
            // 丢弃旧包
            av_packet_unref(m_decodingAudioPacket);
            continue;
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
        SDL_Delay(100);

        // 定时同步时钟源状态到调试信息
        if (m_clockManager && m_debugStats) {
            int display_clock_type = 0;

            // 优先检查是否处于“未知/同步中”状态
            if (m_clockManager->isClockUnknown()) {
                display_clock_type = -1; // 约定 -1 为未知状态
            }
            else {
                // 如果时钟已就绪，则获取实际配置的类型
                MasterClockType type = m_clockManager->getMasterClockType();
                display_clock_type = static_cast<int>(type);
            }

            // 原子写入 DebugStats
            m_debugStats->clock_source_type = display_clock_type;
        }

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

        // 获取队列包数量（作为时间戳不可靠时的保险）
        int video_pkt_count = m_videoPacketQueue ? m_videoPacketQueue->size() : 0;
        int audio_pkt_count = m_audioPacketQueue ? m_audioPacketQueue->size() : 0;

        PlayerState current_state = m_playerState.load();
        bool is_live_stream = m_demuxer && m_demuxer->isLiveStream();

        // --- 决策逻辑 ---
        switch (current_state) {
        case PlayerState::BUFFERING:
        {
            bool demux_finished = m_demuxer_eof.load();

            bool should_play = false;

            if (is_live_stream) {
                // 【直播 策略】
                // 只要有一定数量的包（例如 5 个视频包）就立即播放，降低延迟
                // 或者缓冲时间极短（例如 > 0.1秒）
                if (video_pkt_count > 5 || current_buffer_sec > 0.1) {
                    cout << "MediaPlayer: LIVE stream buffered enough (" << video_pkt_count << " pkts). Playing." << endl;
                    should_play = true;
                }
            }
            else {
                // 【本地文件 策略】2.0秒缓存 或 队列满
                if (current_buffer_sec >= PLAYOUT_THRESHOLD_SEC || demux_finished) {
                    cout << "MediaPlayer: Local file buffered " << current_buffer_sec << "s. Playing." << endl;
                    should_play = true;
                }
            }

            if (should_play) {
                if (m_clockManager) m_clockManager->resume();
                setPlayerState(PlayerState::PLAYING);
                m_state_cond.notify_all();
            }
            break;
        }
        case PlayerState::PLAYING:
            //【针对直播流的防抖动策略】
            {
                bool is_empty = (videoStreamIndex != -1 && video_pkt_count == 0);
                // 仅当完全没数据且流未结束时，才进入缓冲
                if (is_empty && !m_demuxer_eof.load()) {
                    cout << "MediaPlayer: Queue empty. Re-buffering." << endl;
                    setPlayerState(PlayerState::BUFFERING);
                }
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

void MediaPlayer::setPlayerState(PlayerState newState) {
    m_playerState.store(newState);
    if (m_debugStats) {
        // 将 enum 强转为 int 存入调试信息
        m_debugStats->current_state.store(static_cast<int>(newState));
    }
}
