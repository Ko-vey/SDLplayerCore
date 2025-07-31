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
    m_pause(false),
    videoStreamIndex(-1),
    audioStreamIndex(-1),
    m_decodingVideoPacket(nullptr),
    m_renderingVideoFrame(nullptr),
    m_decodingAudioPacket(nullptr),
    m_renderingAudioFrame(nullptr),
    m_demuxThread(nullptr),
    m_videoDecodeThread(nullptr),
    m_videoRenderthread(nullptr),
    m_audioDecodeThread(nullptr),
    m_audioRenderThread(nullptr),
    frame_cnt(0)
{
    cout << "MediaPlayer: Initializing..." << endl;

    // 构造函数的职责是保证：要么成功创建一个完整的对象，要么抛出异常并清理所有已分配的资源。
    // 使用一个总的 try-catch 块来捕获任何初始化阶段的失败。
    try {
        init_components(filepath);
        cout << "MediaPlayer: Initialized successfully. All threads started." << endl;
    }
    catch (const std::exception& e) {
        // 如果init_components的任何一步抛出异常，都会进入这里。
        // 此时，对象构造失败，需要确保所有已启动的、非RAII管理的资源（主要是线程）被正确停止。
        cerr << "MediaPlayer: CRITICAL: Constructor failed: " << e.what() << endl;
        cleanup();
        throw; // 重新抛出异常，通知调用者(main)构造失败。
    }
}

// 初始化流程总调度
void MediaPlayer::init_components(const std::string& filepath) {
    cout << "MediaPlayer: Initializing components..." << endl;

    // 步骤 0: 初始化C++层面的基础组件 (队列, 时钟等)
    // 这些操作如果失败 (如 bad_alloc)，会直接抛出异常。
    const int MAX_VIDEO_PACKETS = 100;
    const int MAX_AUDIO_PACKETS = 100;
    const int MAX_VIDEO_FRAMES = 5;
    const int MAX_AUDIO_FRAMES = 10;

    m_videoPacketQueue = std::make_unique<PacketQueue>(MAX_VIDEO_PACKETS);
    m_audioPacketQueue = std::make_unique<PacketQueue>(MAX_AUDIO_PACKETS);
    m_videoFrameQueue = std::make_unique<FrameQueue>(MAX_VIDEO_FRAMES);
    m_audioFrameQueue = std::make_unique<FrameQueue>(MAX_AUDIO_FRAMES);

    m_clockManager = std::make_unique<ClockManager>();
    cout << "MediaPlayer: Queues and clock manager created." << endl;

    // 步骤 1: 初始化所有FFmpeg相关资源
    init_ffmpeg_resources(filepath);
    // 确定流信息后，初始化时钟管理器
    if (m_clockManager) {
        bool has_audio = (audioStreamIndex >= 0);
        bool has_video = (videoStreamIndex >= 0);
        m_clockManager->init(has_audio, has_video);
    }

    // 步骤 2: 初始化所有SDL相关资源 (渲染器)
    init_sdl_video_renderer();
    init_sdl_audio_renderer();

    // 步骤 3: 所有资源准备就绪，最后启动工作线程
    // 这一步最容易失败且最难回滚，所以放在最后。
    start_threads();
}

// 封装FFmpeg资源的初始化
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

// 初始化SDL视频渲染器
void MediaPlayer::init_sdl_video_renderer() {
    cout << "MediaPlayer: Initializing SDL renderer..." << endl;

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
        if (!sdl_renderer->initForAudioOnly("SDLplayerCore (Audio)", 640, 480, m_clockManager.get())) {
            throw std::runtime_error("SDL Init Error: Failed to initialize audio-only window.");
        }
    }
    // 如果视频和音频流都没有，则不创建渲染器
    else {
        cout << "MediaPlayer: No video or audio streams found. Skipping renderer initialization." << endl;
        return; // 在这种情况下，m_videoRenderer 将保持 nullptr
    }

    // 初始化成功，将所有权转移给成员变量
    m_videoRenderer = std::move(sdl_renderer);
    cout << "MediaPlayer: SDL renderer component initialized successfully." << endl;
}

// 初始化SDL音频渲染器
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

    // 启动解封装线程
    m_demuxThread = SDL_CreateThread(demux_thread_entry, "DemuxThread", this);
    if (!m_demuxThread) throw std::runtime_error("Thread Error: Could not create demux thread.");

    // 启动视频解码线程
    if (videoStreamIndex >= 0) {
        m_videoDecodeThread = SDL_CreateThread(video_decode_thread_entry, "VideoDecodeThread", this);
        // 解码线程创建失败，但解复用线程已经启动！必须在抛出异常前通知它退出
        if (!m_videoDecodeThread) throw std::runtime_error("Thread Error: Could not create video decode thread.");
        // 视频渲染线程 (m_videoRenderthread) 在 runMainLoop 中启动，不属于构造阶段
    }
    // 启动音频解码线程
    if (audioStreamIndex >= 0) {
        m_audioDecodeThread = SDL_CreateThread(audio_decode_thread_entry, "AudioDecodeThread", this);
        if (!m_audioDecodeThread) throw std::runtime_error("Thread Error: Could not create audio decode thread.");
        m_audioRenderThread = SDL_CreateThread(audio_render_thread_entry, "AudioRenderThread", this);
        if (!m_audioRenderThread) throw std::runtime_error("Thread Error: Failed to create audio render thread.");
    }

    cout << "MediaPlayer: Demux, video decode, and audio decode/render threads started." << endl;
}

MediaPlayer::~MediaPlayer() {
    cout << "MediaPlayer: Destructing..." << endl;
    cleanup();
    cout << "MediaPlayer: Destruction complete." << endl;
}

int MediaPlayer::init_demuxer_and_decoders(const string& filepath) {
    cout << "MediaPlayer: Initializing Demuxer and Decoders for: " << filepath << endl;

    // 文件路径验证
    if (filepath.empty()) {
        cerr << "FFmpeg Init Error: Filepath is empty." << endl;
        return -1;
    }
    std::ifstream file_test(filepath, std::ios::binary);
    if (!file_test) {
        cerr << "FFmpeg Init Error: Cannot open file (ifstream check failed): \"" << filepath << "\"." << endl;
        file_test.close();
        return -1;
    }
    file_test.close();
    cout << "Filepath validated. Initializing FFmpeg components." << endl;

    //1、创建并打开 解封装器Demuxer
    m_demuxer = std::make_unique<FFmpegDemuxer>();
    if (!m_demuxer->open(filepath.c_str())) {
        cerr << "MediaPlayer Error: Demuxer failed to open file: " << filepath << endl;
        return -1;
    }
    cout << "MediaPlayer: Demuxer opened successfully." << endl;

    // 2、查找所有可能的流
    videoStreamIndex = m_demuxer->findStream(AVMEDIA_TYPE_VIDEO);
    audioStreamIndex = m_demuxer->findStream(AVMEDIA_TYPE_AUDIO);

    // 3. 检查是否至少有一个可播放的流
    if (videoStreamIndex < 0 && audioStreamIndex < 0) {
        cerr << "MediaPlayer Error: Demuxer didn't find any video or audio streams." << endl;
        m_demuxer->close(); // 显式关闭
        return -1;          // 致命错误
    }

    // 4. 如果视频流存在，则初始化视频解码器
    if (videoStreamIndex >= 0) {
        cout << "MediaPlayer: Video stream found at index: " << videoStreamIndex << endl;
        AVCodecParameters* pVideoCodecParams = m_demuxer->getCodecParameters(videoStreamIndex);
        if (!pVideoCodecParams) {
            cerr << "MediaPlayer Warning: Demuxer failed to get codec parameters for video stream. Ignoring video." << endl;
            videoStreamIndex = -1; // 获取参数失败，也视为无视频流
        }
        else if (!m_videoDecoder->init(pVideoCodecParams)) {
            cerr << "MediaPlayer Warning: Failed to initialize video decoder. Ignoring video." << endl;
            videoStreamIndex = -1; // 初始化失败，也视为无视频流
        }
        else {
            cout << "MediaPlayer: Video decoder initialized successfully." << endl;
            cout << "MediaPlayer: Decoder details - Width: " << m_videoDecoder->getWidth()
                << ", Height: " << m_videoDecoder->getHeight()
                << ", Format: " << av_get_pix_fmt_name(m_videoDecoder->getPixelFormat()) << endl;
        }
    }
    else {
        cout << "MediaPlayer: No video stream found." << endl;
    }

    // 5. 如果音频流存在，则初始化音频解码器
    if (audioStreamIndex >= 0) {
        cout << "MediaPlayer: Audio stream found at index: " << audioStreamIndex << endl;
        AVCodecParameters* pAudioCodecParams = m_demuxer->getCodecParameters(audioStreamIndex);

        // 1. 获取正确的 time_base
        AVRational audioTimeBase = m_demuxer->getTimeBase(audioStreamIndex);

        if (!pAudioCodecParams) {
            cerr << "MediaPlayer Warning: Demuxer failed to get codec parameters for audio stream. Ignoring audio." << endl;
            audioStreamIndex = -1; // 将索引置为无效
        }
        else if (!m_audioDecoder->init(pAudioCodecParams, audioTimeBase, m_clockManager.get())) {
            cerr << "MediaPlayer Warning: Failed to initialize audio decoder. Ignoring audio." << endl;
            m_audioDecoder.reset(); // 初始化失败，释放解码器
            audioStreamIndex = -1; // 将索引置为无效
        }
        else {
            cout << "MediaPlayer: Audio decoder initialized successfully." << endl;
        }
    }
    else {
        cout << "MediaPlayer: No audio stream found." << endl;
    }

    // 6. 再次检查，如果经过初始化后所有流都失效了，也应报错退出
    if (videoStreamIndex < 0 && audioStreamIndex < 0) {
        cerr << "MediaPlayer Error: Failed to initialize any valid decoders." << endl;
        m_demuxer->close();
        return -1;
    }

    // 7. 若存在视频流，准备视频相关资源
    if (videoStreamIndex >= 0) {
        cout << "MediaPlayer: Preparing video-specific resources (SWS context etc.)." << endl;
        // 从已经初始化的解码器获取维度和像素格式
        int dec_width = m_videoDecoder->getWidth();
        int dec_height = m_videoDecoder->getHeight();

        if (dec_width <= 0 || dec_height <= 0) {
            cerr << "MediaPlayer Error: Decoder returned invalid dimensions (" << dec_width << "x" << dec_height << ")." << endl;
            m_demuxer->close();
            return -1;  // 视频初始化错误
        }
        // (未来若需要)可在此添加 SWS Context 的初始化代码
    }

    cout << "MediaPlayer: FFmpeg demuxer and decoders initialization process finished." << endl;
    return 0;
}


int MediaPlayer::handle_event(const SDL_Event& event) {
    switch (event.type) {
    // 关闭按钮
    case SDL_QUIT:
        cout << "MediaPlayer: SDL_QUIT event received, requesting quit." << endl;
        m_quit = true;
        return 0;   //表示退出

    case SDL_KEYDOWN:
        // ESC退出
        if (event.key.keysym.sym == SDLK_ESCAPE) {
            cout << "MediaPlayer: Escape key pressed, requesting quit." << endl;
            m_quit = true;
            return 0;
        }
        // 空格键暂停
        if (event.key.keysym.sym == SDLK_SPACE) {
            if (m_pause) {                  // 从暂停到播放
                m_clockManager->resume();   // 内部调用 SDL_PauseAudioDevice(..., 0)
                m_pause = false;            // 保持内部m_pause标志同步
                cout << "MediaPlayer: Resumed." << endl;
                m_pause_cond.notify_all();  // 通知所有等待在 m_pause_cond 上的线程，可以继续了
            }
            else {                          // 从播放到暂停
                m_clockManager->pause();    // 内部调用 SDL_PauseAudioDevice(..., 1)
                m_pause = true;             // 保持内部m_pause标志同步
                cout << "MediaPlayer: Paused." << endl;
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
                if (!m_videoRenderer->onWindowResize(newWidth, newHeight)) {
                    cerr << "MediaPlayer: Failed to handle window resize." << endl;
                }
                m_videoRenderer->refresh(); // 立即刷新显示
            }
        }
        // 窗口恢复事件处理
        else if (event.window.event == SDL_WINDOWEVENT_RESTORED) {
            cout << "MediaPlayer: Window restored, refreshing display." << endl;
            if (m_videoRenderer) {
                m_videoRenderer->refresh();
            }
        }
        // 窗口获得焦点事件处理  
        else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            cout << "MediaPlayer: Window focus gained, refreshing display." << endl;
            if (m_videoRenderer) {
                m_videoRenderer->refresh();
            }
        }
        // 窗口显示事件处理
        else if (event.window.event == SDL_WINDOWEVENT_SHOWN) {
            cout << "MediaPlayer: Window shown, refreshing display." << endl;
            if (m_videoRenderer) {
                m_videoRenderer->refresh();
            }
        }
        // 窗口暴露事件处理（窗口需要重绘时）
        else if (event.window.event == SDL_WINDOWEVENT_EXPOSED) {
            cout << "MediaPlayer: Window exposed, refreshing display." << endl;
            if (m_videoRenderer) {
                m_videoRenderer->refresh();
            }
        }
        else if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
            cout << "MediaPlayer: Window close event received, requesting quit." << endl;
            m_quit = true;
            return 0;
        }
        break;

    case REFRESH_EVENT: // 如果不使用轮询/延迟循环，则可以使用此事件来触发渲染
        break;

    case BREAK_EVENT:// 如果需要，从 SDL_Delay 唤醒线程
        cout << "MediaPlayer: Break event received." << endl;
        break;

    default:
        break;
    }
    return 1; // 表示继续
}

// 基础主循环启动函数
int MediaPlayer::runMainLoop() {
    cout << "MediaPlayer: Starting main loop (event handling and video render trigger)." << endl;

    // 只在有视频流时才启动视频渲染线程
    if (videoStreamIndex >= 0) {
        // 启动视频渲染线程 (在循环前启动)
        // 视频渲染线程在此启动（而不是构造阶段），是为了将“对象构建/数据准备”与“对象运行/用户交互”分离
        m_videoRenderthread = SDL_CreateThread(video_render_thread_entry, "VideoRenderThread", this);
        if (!m_videoRenderthread) {
            cerr << "MediaPlayer Error: Could not create video render thread." << endl;
            m_quit = true; // 向其他线程发出退出信号
            // 确保在返回错误之前其他线程已加入
            if (m_videoDecodeThread) SDL_WaitThread(m_videoDecodeThread, nullptr);
            if (m_demuxThread) SDL_WaitThread(m_demuxThread, nullptr);
            return -1;
        }
    }

    SDL_Event event;
    Uint64 last_window_check = SDL_GetTicks64();
    // 增加检查间隔，因为渲染线程也在检查，此处只是保底
    const Uint64 WINDOW_CHECK_INTERVAL = 1000;      // 每秒检查一次窗口状态

    while (!m_quit) {
        // 处理 SDL 事件
        // 使用 SDL_WaitEventTimeout 避免CPU空转
        if (SDL_WaitEventTimeout(&event, 100)) {    // 等待最多 100 毫秒的事件
            if (handle_event(event) == 0) {         // handle_event 返回 0 退出
                m_quit = true;                      // 如果 handle_event 决定退出，请确保设置 m_quit
            }
        }

        // 定期检查窗口状态（即使没有事件），作为保底刷新机制
        Uint64 current_time = SDL_GetTicks64();
        if (current_time - last_window_check > WINDOW_CHECK_INTERVAL) {
            if (m_videoRenderer && !m_pause) { // 暂停时不需要主循环主动刷新
                // 主动刷新显示，确保在没有焦点时也能正常显示
                //cout << "MediaPlayer MainLoop: Periodic video render check." << endl;
                m_videoRenderer->refresh();
            }
            last_window_check = current_time;
        }
        // 再次检查 m_quit 以防它被另一个线程设置或者发生超时
        if (m_quit) break;
    }

    cout << "MediaPlayer: Main loop requested to quit." << endl;
    // m_videoRenderthread 将在析构函数中被join。
    // 其他线程（demux、video_decode）也会检查 m_quit 并退出。
    // 它们的join也在析构函数中处理。
    return 0;
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
        // av_frame_free 会自动执行 unref
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

    // 1、发送退出信号
    m_quit = true;
    // 通知后立即释放锁
    {
        std::lock_guard<std::mutex> lock(m_pause_mutex);
        m_pause_cond.notify_all(); // 唤醒可能在暂停中等待的线程
    }
    // 唤醒等待队列的线程
    if (m_videoPacketQueue) m_videoPacketQueue->signal_eof();
    if (m_audioPacketQueue) m_audioPacketQueue->signal_eof();
    if (m_videoFrameQueue) m_videoFrameQueue->signal_eof();
    if (m_audioFrameQueue) m_audioFrameQueue->signal_eof();

    // 2、等待所有线程结束
    // 先等待生产者线程，再等待消费者线程，避免潜在死锁
    cout << "MediaPlayer: Waiting for threads to finish..." << endl;
    if (m_demuxThread) {
        cout << "MediaPlayer: Waiting for demux thread to finish..." << endl;
        SDL_WaitThread(m_demuxThread, nullptr);
        cout << "MediaPlayer: Demux thread finished." << endl;
    }
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
    if (m_videoRenderthread) {
        cout << "MediaPlayer: Waiting for refresh thread to finish..." << endl;
        SDL_WaitThread(m_videoRenderthread, nullptr);
        cout << "MediaPlayer: Refresh thread finished." << endl;
    }
    if (m_audioRenderThread) {
        cout << "MediaPlayer: Waiting for audio render thread to finish..." << endl;
        SDL_WaitThread(m_audioRenderThread, nullptr);
        cout << "MediaPlayer: Audio render thread finished." << endl;
    }
    cout << "MediaPlayer: All threads have been joined. Cleaning up resources..." << endl;
    // 线程已全部停止，按逻辑顺序释放所有资源

    // 3. 释放SDL渲染器
    // 销毁Texture, Renderer, Window
    // SDL渲染器依赖FFmpeg信息，必须先于FFmpeg清理
    if (m_audioRenderer) {
        m_audioRenderer.reset();
        cout << "MediaPlayer: Audio Renderer cleaned up." << endl;
    }
    if (m_videoRenderer) {
        m_videoRenderer.reset();
        cout << "MediaPlayer: Video Renderer cleaned up." << endl;
    }

    // 4. 释放FFmpeg核心资源
    cleanup_ffmpeg_resources();

    // 释放队列和时钟
    // 队列中可能还存有 AVPacket/AVFrame，它们的清理依赖 FFmpeg 库
    // 所以在 cleanup_ffmpeg_resources 之后进行
    if (m_videoPacketQueue) {
        m_videoPacketQueue.reset();
        cout << "MediaPlayer: Video packet queue cleaned up." << endl;
    }
    if (m_videoFrameQueue) {
        m_videoFrameQueue.reset();
        cout << "MediaPlayer: Video frame queue cleaned up." << endl;
    }
    if (m_audioPacketQueue) {
        m_audioPacketQueue.reset();
        cout << "MediaPlayer: Audio packet queue cleaned up." << endl;
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
    // 获取MediaPlayer实例指针
    return static_cast<MediaPlayer*>(opaque)->demux_thread_func();
}

int MediaPlayer::demux_thread_func() {
    cout << "MediaPlayer: Demux thread started." << endl;
    AVPacket* demux_packet = av_packet_alloc();//本地包，用于从解复用器读取
    if (!demux_packet) {
        cerr << "MediaPlayer DemuxThread Error: Could not allocate demux_packet." << endl;
        if (m_videoPacketQueue) { m_videoPacketQueue->signal_eof(); } // 将 错误 作为EOF 进行传递
        if (m_audioPacketQueue) { m_audioPacketQueue->signal_eof(); }
        m_quit = true;  // 严重错误，请求主播放器退出
        return -1;
    }

    int read_ret = 0;
    while (!m_quit) {
        // 暂停处理逻辑
        // 使用作用域来确保 等待 wait 结束后、执行耗时任务前释放锁 m_pause_mutex
        {
            std::unique_lock<std::mutex> lock(m_pause_mutex);
            // wait会检查条件：如果 m_pause 为 true 且 m_quit 为 false，线程会在此阻塞
            // 直到 m_pause_cond.notify_all() 被调用，它才会醒来重新检查条件
            m_pause_cond.wait(lock, [this] { return !m_pause || m_quit; });
        }
        // 如果是因退出而被唤醒，则直接退出循环
        if (m_quit) break;

        read_ret = m_demuxer->readPacket(demux_packet);

        if (read_ret < 0) {
            if (read_ret == AVERROR_EOF) {
                cout << "MediaPlayer DemuxThread: Demuxer reached EOF." << endl;
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
            break;//退出解复用循环
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

    av_packet_free(&demux_packet);//释放本地包
    cout << "MediaPlayer: Demux thread finished." << endl;

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

int MediaPlayer::video_decode_func() {
    cout << "MediaPlayer: Video decode thread started." << endl;
    if (!m_videoDecoder || !m_videoPacketQueue || !m_videoFrameQueue) {
        cerr << "MediaPlayer VideoDecodeThread Error: Decoder or queues not initialized." << endl;
        if (m_videoFrameQueue) m_videoFrameQueue->signal_eof();
        return -1;
    }

    AVFrame* decoded_frame = nullptr; // 将由 m_videoDecoder->decode() 分配

    while (!m_quit) {
        // 暂停处理逻辑
        {
            std::unique_lock<std::mutex> lock(m_pause_mutex);
            m_pause_cond.wait(lock, [this] { return !m_pause || m_quit; });
        }
        if (m_quit) break;

        if (!m_videoPacketQueue->pop(m_decodingVideoPacket, 100)) {
            if (m_videoPacketQueue->is_eof()) {
                cout << "MediaPlayer VideoDecodeThread: Packet queue EOF, starting to flush decoder." << endl;
                int flush_ret = m_videoDecoder->decode(nullptr, &decoded_frame); // 发送 nullptr 来冲洗
                while (flush_ret == 0) {
                    if (decoded_frame) {
                        if (!m_videoFrameQueue->push(decoded_frame)) { // FrameQueue::push 会 ref
                            cerr << "MediaPlayer VideoDecodeThread: Failed to push flushed frame to frame queue." << endl;
                        }
                        av_frame_free(&decoded_frame); // 释放 decode() 分配的 shell
                        // decoded_frame 现在无效
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
                continue;
            }
        }

        int decode_ret = m_videoDecoder->decode(m_decodingVideoPacket, &decoded_frame);
        av_packet_unref(m_decodingVideoPacket);

        if (decode_ret == 0) {
            if (decoded_frame) {
                if (!m_videoFrameQueue->push(decoded_frame)) { // FrameQueue::push 会 ref
                    cerr << "MediaPlayer VideoDecodeThread: Failed to push decoded frame to frame queue." << endl;
                }
                av_frame_free(&decoded_frame); // <--- 释放 decode() 分配的 shell
                // decoded_frame 现在无效
            }
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

    cout << "MediaPlayer: Video decode thread finished." << endl;
    return 0;
}

// 视频渲染线程入口和主函数
int MediaPlayer::video_render_thread_entry(void* opaque) {
    return static_cast<MediaPlayer*>(opaque)->video_render_func();
}

int MediaPlayer::video_render_func() {
    cout << "MediaPlayer: VideoRenderThread started." << endl;
    if (!m_renderingVideoFrame) { // 确保 shell 已分配
        cerr << "MediaPlayer VideoRenderThread Error: m_renderingVideoFrame is null." << endl;
        return -1;
    }

    // 状态跟踪变量
    Uint64 last_refresh_time = SDL_GetTicks64();
    const Uint64 FORCE_REFRESH_INTERVAL = 500;  // 强制刷新间隔(ms)

    while (!m_quit) {
        // 暂停处理逻辑
        {
            std::unique_lock<std::mutex> lock(m_pause_mutex);
            m_pause_cond.wait(lock, [this] { return !m_pause || m_quit; });
        }
        if (m_quit) break;

        // 尝试从队列获取新帧；使用带超时的pop，防止永久阻塞
        bool got_new_frame = m_videoFrameQueue->pop(m_renderingVideoFrame, 100);

        if (got_new_frame) {
            // 有新帧，正常渲染
            if (!m_videoRenderer->renderFrame(m_renderingVideoFrame)) {
                cerr << "MediaPlayer VideoRenderThread: renderFrame failed." << endl;
                m_quit = true; // 渲染出错，退出播放
            }
            frame_cnt++;
            av_frame_unref(m_renderingVideoFrame);  // 使用后 unref 解引用，以便 shell 重用
            last_refresh_time = SDL_GetTicks64();   // 成功渲染后更新刷新时间
        }
        else {
            // 获取帧失败 (超时或EOF)
            if (m_videoFrameQueue->is_eof()) {
                cout << "MediaPlayer VideoRenderThread: Frame queue EOF and empty. Exiting." << endl;
                break;
            }

            // 没有新帧时，检查是否需要强制刷新
            Uint64 current_time = SDL_GetTicks64();
            bool should_force_refresh = false;

            // 1. 基于时间的定期刷新
            if (current_time - last_refresh_time > FORCE_REFRESH_INTERVAL) {
                should_force_refresh = true;
            }

            // 2. 基于窗口状态的主动检查
            if (m_videoRenderer) {
                // 向下转型以调用派生类特有的方法
                // dynamic_cast 在程序运行时进行类型转换，用于执行安全的向下转型（向子类）
                auto* sdl_renderer = dynamic_cast<SDLVideoRenderer*>(m_videoRenderer.get());
                if (sdl_renderer) {
                    SDL_Window* window = sdl_renderer->getWindow();
                    if (window) {
                        Uint32 flags = SDL_GetWindowFlags(window);
                        // 如果窗口可见且未最小化，就值得刷新
                        if ((flags & SDL_WINDOW_SHOWN) && !(flags & SDL_WINDOW_MINIMIZED)) {
                            // 这个条件可以触发刷新，即便时间间隔未到
                        }
                        else {
                            // 如果窗口被隐藏或最小化，则没必要刷新
                            should_force_refresh = false;
                        }
                    }
                }
            }
            // 没有新帧但需要强制刷新
            if (should_force_refresh && m_videoRenderer) {
                cout << "MediaPlayer: Force refreshing display (no new frame)." << endl;
                m_videoRenderer->refresh();
                last_refresh_time = current_time; // 刷新后更新时间
            }
        }
    }
    cout << "MediaPlayer: Video render thread finished. Total frames rendered: " << get_frame_cnt() << endl;
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

    AVFrame* decoded_frame = nullptr; // 将由 m_audioDecoder->decode() 分配

    while (!m_quit) {
        // 暂停处理逻辑
        {
            std::unique_lock<std::mutex> lock(m_pause_mutex);
            m_pause_cond.wait(lock, [this] { return !m_pause || m_quit; });
        }
        if (m_quit) break;

        // 1. 从音频包队列中取出一个包
        if (!m_audioPacketQueue->pop(m_decodingAudioPacket, 100)) {
            // 如果pop失败，检查是否是由于EOF
            if (m_audioPacketQueue->is_eof()) {
                cout << "MediaPlayer AudioDecodeThread: Packet queue EOF, starting to flush decoder." << endl;

                // 发送 nullptr 来刷新解码器
                int flush_ret = m_audioDecoder->decode(nullptr, &decoded_frame);
                while (flush_ret == 0) { // 持续获取帧直到解码器无更多输出
                    if (decoded_frame) {
                        if (!m_audioFrameQueue->push(decoded_frame)) { // FrameQueue::push 会 ref
                            cerr << "MediaPlayer AudioDecodeThread: Failed to push flushed frame to frame queue." << endl;
                        }
                        // decode() 分配了新的帧或重用了旧的，必须释放其外壳(shell)
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
                // pop超时，继续循环
                continue;
            }
        }

        // 2. 解码数据包
        int decode_ret = m_audioDecoder->decode(m_decodingAudioPacket, &decoded_frame);
        av_packet_unref(m_decodingAudioPacket); // 解码后不再需要此数据包

        if (decode_ret == 0) {
            if (decoded_frame) {
                if (!m_audioFrameQueue->push(decoded_frame)) {
                    cerr << "MediaPlayer AudioDecodeThread: Failed to push decoded frame to frame queue." << endl;
                }
                // 释放帧外壳
                av_frame_free(&decoded_frame);
            }
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

    cout << "MediaPlayer: Audio decode thread finished." << endl;
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
        // 暂停处理逻辑
        {
            std::unique_lock<std::mutex> lock(m_pause_mutex);
            m_pause_cond.wait(lock, [this] { return !m_pause || m_quit; });
        }
        if (m_quit) break;

        // 从音频帧队列中取出一帧，设置100ms超时
        if (!m_audioFrameQueue->pop(m_renderingAudioFrame, 100)) {
            if (m_audioFrameQueue->is_eof()) {
                cout << "MediaPlayer AudioRenderThread: Frame queue EOF and empty. Exiting." << endl;
                break;
            }
            continue; // 超时，继续循环检查退出和暂停状态
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
    cout << "MediaPlayer: Audio render thread finished." << endl;
    return 0;
}
