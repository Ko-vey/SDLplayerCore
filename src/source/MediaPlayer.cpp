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

#include <fstream>      // �ļ�·����֤
#include <stdexcept>    // std::runtime_error
#include <chrono>       // SDL_Delay ���� PacketQueue ��ʱ

// PacketQueue.h �� FrameQueue.h ͨ�� MediaPlayer.h ����
#include "../include/MediaPlayer.h"
#include "../include/FFmpegDemuxer.h"
#include "../include/FFmpegVideoDecoder.h"
#include "../include/FFmpegAudioDecoder.h"
#include "../include/SDLVideoRenderer.h"
#include "../include/SDLAudioRenderer.h"
#include "../include/ClockManager.h"

using namespace std;

// ��ʼ��������������������̣߳�ʧ��ʱ�׳� std::runtime_error
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

    // ���캯����ְ���Ǳ�֤��Ҫô�ɹ�����һ�������Ķ���Ҫô�׳��쳣�����������ѷ������Դ��
    // ʹ��һ���ܵ� try-catch ���������κγ�ʼ���׶ε�ʧ�ܡ�
    try {
        init_components(filepath);
        cout << "MediaPlayer: Initialized successfully. All threads started." << endl;
    }
    catch (const std::exception& e) {
        // ���init_components���κ�һ���׳��쳣������������
        // ��ʱ��������ʧ�ܣ���Ҫȷ�������������ġ���RAII�������Դ����Ҫ���̣߳�����ȷֹͣ��
        cerr << "MediaPlayer: CRITICAL: Constructor failed: " << e.what() << endl;
        cleanup();
        throw; // �����׳��쳣��֪ͨ������(main)����ʧ�ܡ�
    }
}

// ��ʼ�������ܵ���
void MediaPlayer::init_components(const std::string& filepath) {
    cout << "MediaPlayer: Initializing components..." << endl;

    // ���� 0: ��ʼ��C++����Ļ������ (����, ʱ�ӵ�)
    // ��Щ�������ʧ�� (�� bad_alloc)����ֱ���׳��쳣��
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

    // ���� 1: ��ʼ������FFmpeg�����Դ
    init_ffmpeg_resources(filepath);
    // ȷ������Ϣ�󣬳�ʼ��ʱ�ӹ�����
    if (m_clockManager) {
        bool has_audio = (audioStreamIndex >= 0);
        bool has_video = (videoStreamIndex >= 0);
        m_clockManager->init(has_audio, has_video);
    }

    // ���� 2: ��ʼ������SDL�����Դ (��Ⱦ��)
    init_sdl_video_renderer();
    init_sdl_audio_renderer();

    // ���� 3: ������Դ׼��������������������߳�
    // ��һ��������ʧ�������ѻع������Է������
    start_threads();
}

// ��װFFmpeg��Դ�ĳ�ʼ��
void MediaPlayer::init_ffmpeg_resources(const std::string& filepath) {
    cout << "MediaPlayer: Initializing FFmpeg resources..." << endl;

    // Ϊ����������Ⱦ��������ָ���װ�� AVFrame/AVPacket
    // ��Щ��Դ��Ҫ�ֶ������������ڣ����������쳣�������ͷ�
    m_decodingVideoPacket = av_packet_alloc();
    if (!m_decodingVideoPacket) throw std::runtime_error("FFmpeg Init Error: Could not allocate video decoding packet.");

    m_decodingAudioPacket = av_packet_alloc();
    if (!m_decodingAudioPacket) throw std::runtime_error("FFmpeg Init Error: Could not allocate audio decoding packet.");

    m_renderingVideoFrame = av_frame_alloc();
    if (!m_renderingVideoFrame) throw std::runtime_error("FFmpeg Init Error: Could not allocate video rendering frame.");

    m_renderingAudioFrame = av_frame_alloc();
    if (!m_renderingAudioFrame) throw std::runtime_error("FFmpeg Init Error: Could not allocate audio rendering frame");

    // ����������ʵ�� (��ʱֻ�ǿտ�)
    m_videoDecoder = std::make_unique<FFmpegVideoDecoder>();
    m_audioDecoder = std::make_unique<FFmpegAudioDecoder>();

    // ���ü��ɵĽ⸴�����ͽ�������ʼ������
    if (init_demuxer_and_decoders(filepath) != 0) {
        // ������������ֱ���׳��쳣������catch�鴦��
        throw std::runtime_error("FFmpeg Init Error: Demuxer/Decoder initialization failed.");
    }
    cout << "MediaPlayer: FFmpeg resources initialized successfully." << endl;
}

// ��ʼ��SDL��Ƶ��Ⱦ��
void MediaPlayer::init_sdl_video_renderer() {
    cout << "MediaPlayer: Initializing SDL renderer..." << endl;

    // ���Ǵ��� SDLVideoRenderer ʵ��
    auto sdl_renderer = std::make_unique<SDLVideoRenderer>();

    // �������Ƶ���������������ʼ��
    if (videoStreamIndex >= 0) {
        cout << "MediaPlayer: Video stream found. Initializing full video renderer." << endl;

        // ���ѳ�ʼ���Ľ�������ȡ��Ƶ�ߴ�
        int video_width = m_videoDecoder->getWidth();
        int video_height = m_videoDecoder->getHeight();
        if (video_width <= 0 || video_height <= 0) {
            throw std::runtime_error("SDL Init Error: Video decoder did not provide valid dimensions.");
        }

        if (!sdl_renderer->init("SDLplayerCore (Video)", video_width, video_height,
            m_videoDecoder->getPixelFormat(), m_clockManager.get())) {
            throw std::runtime_error("SDL Init Error: Failed to initialize SDL Video Renderer.");
        }

        // ����ͬ�������ʱ�Ӳ���
        AVStream* video_stream = m_demuxer->getFormatContext()->streams[videoStreamIndex];
        if (video_stream) {
            sdl_renderer->setSyncParameters(video_stream->time_base, av_q2d(video_stream->avg_frame_rate));
        }
    }
    // ���û����Ƶ����������Ƶ��������д���Ƶģʽ�ĳ�ʼ��
    else if (audioStreamIndex >= 0) {
        cout << "MediaPlayer: No video stream. Initializing in audio-only mode." << endl;
        // ʹ��Ĭ�ϳߴ紴��һ�����ڽ����Ĵ���
        if (!sdl_renderer->initForAudioOnly("SDLplayerCore (Audio)", 640, 480, m_clockManager.get())) {
            throw std::runtime_error("SDL Init Error: Failed to initialize audio-only window.");
        }
    }
    // �����Ƶ����Ƶ����û�У��򲻴�����Ⱦ��
    else {
        cout << "MediaPlayer: No video or audio streams found. Skipping renderer initialization." << endl;
        return; // ����������£�m_videoRenderer ������ nullptr
    }

    // ��ʼ���ɹ���������Ȩת�Ƹ���Ա����
    m_videoRenderer = std::move(sdl_renderer);
    cout << "MediaPlayer: SDL renderer component initialized successfully." << endl;
}

// ��ʼ��SDL��Ƶ��Ⱦ��
void MediaPlayer::init_sdl_audio_renderer() {
    if (audioStreamIndex < 0) {
        cout << "MediaPlayer: No audio stream found. Skipping audio renderer initialization." << endl;
        return;
    }
    cout << "MediaPlayer: Initializing SDL Audio Renderer..." << endl;

    m_audioRenderer = std::make_unique<SDLAudioRenderer>();

    // �ӽ�������ȡ��Ƶ����
    int sampleRate = m_audioDecoder->getSampleRate();
    int channels = m_audioDecoder->getChannels();
    AVSampleFormat sampleFmt = m_audioDecoder->getSampleFormat();
    AVRational timeBase = m_audioDecoder->getTimeBase();

    if (!m_audioRenderer->init(sampleRate, channels, sampleFmt, timeBase, m_clockManager.get())) {
        throw std::runtime_error("Failed to initialize SDLAudioRenderer");
    }

    cout << "MediaPlayer: SDL Audio Renderer initialized." << endl;
}

// ��װ�̵߳�����
void MediaPlayer::start_threads() {
    cout << "MediaPlayer: Starting worker threads..." << endl;

    // �������װ�߳�
    m_demuxThread = SDL_CreateThread(demux_thread_entry, "DemuxThread", this);
    if (!m_demuxThread) throw std::runtime_error("Thread Error: Could not create demux thread.");

    // ������Ƶ�����߳�
    if (videoStreamIndex >= 0) {
        m_videoDecodeThread = SDL_CreateThread(video_decode_thread_entry, "VideoDecodeThread", this);
        // �����̴߳���ʧ�ܣ����⸴���߳��Ѿ��������������׳��쳣ǰ֪ͨ���˳�
        if (!m_videoDecodeThread) throw std::runtime_error("Thread Error: Could not create video decode thread.");
        // ��Ƶ��Ⱦ�߳� (m_videoRenderthread) �� runMainLoop �������������ڹ���׶�
    }
    // ������Ƶ�����߳�
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

    // �ļ�·����֤
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

    //1���������� ���װ��Demuxer
    m_demuxer = std::make_unique<FFmpegDemuxer>();
    if (!m_demuxer->open(filepath.c_str())) {
        cerr << "MediaPlayer Error: Demuxer failed to open file: " << filepath << endl;
        return -1;
    }
    cout << "MediaPlayer: Demuxer opened successfully." << endl;

    // 2���������п��ܵ���
    videoStreamIndex = m_demuxer->findStream(AVMEDIA_TYPE_VIDEO);
    audioStreamIndex = m_demuxer->findStream(AVMEDIA_TYPE_AUDIO);

    // 3. ����Ƿ�������һ���ɲ��ŵ���
    if (videoStreamIndex < 0 && audioStreamIndex < 0) {
        cerr << "MediaPlayer Error: Demuxer didn't find any video or audio streams." << endl;
        m_demuxer->close(); // ��ʽ�ر�
        return -1;          // ��������
    }

    // 4. �����Ƶ�����ڣ����ʼ����Ƶ������
    if (videoStreamIndex >= 0) {
        cout << "MediaPlayer: Video stream found at index: " << videoStreamIndex << endl;
        AVCodecParameters* pVideoCodecParams = m_demuxer->getCodecParameters(videoStreamIndex);
        if (!pVideoCodecParams) {
            cerr << "MediaPlayer Warning: Demuxer failed to get codec parameters for video stream. Ignoring video." << endl;
            videoStreamIndex = -1; // ��ȡ����ʧ�ܣ�Ҳ��Ϊ����Ƶ��
        }
        else if (!m_videoDecoder->init(pVideoCodecParams)) {
            cerr << "MediaPlayer Warning: Failed to initialize video decoder. Ignoring video." << endl;
            videoStreamIndex = -1; // ��ʼ��ʧ�ܣ�Ҳ��Ϊ����Ƶ��
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

    // 5. �����Ƶ�����ڣ����ʼ����Ƶ������
    if (audioStreamIndex >= 0) {
        cout << "MediaPlayer: Audio stream found at index: " << audioStreamIndex << endl;
        AVCodecParameters* pAudioCodecParams = m_demuxer->getCodecParameters(audioStreamIndex);

        // 1. ��ȡ��ȷ�� time_base
        AVRational audioTimeBase = m_demuxer->getTimeBase(audioStreamIndex);

        if (!pAudioCodecParams) {
            cerr << "MediaPlayer Warning: Demuxer failed to get codec parameters for audio stream. Ignoring audio." << endl;
            audioStreamIndex = -1; // ��������Ϊ��Ч
        }
        else if (!m_audioDecoder->init(pAudioCodecParams, audioTimeBase, m_clockManager.get())) {
            cerr << "MediaPlayer Warning: Failed to initialize audio decoder. Ignoring audio." << endl;
            m_audioDecoder.reset(); // ��ʼ��ʧ�ܣ��ͷŽ�����
            audioStreamIndex = -1; // ��������Ϊ��Ч
        }
        else {
            cout << "MediaPlayer: Audio decoder initialized successfully." << endl;
        }
    }
    else {
        cout << "MediaPlayer: No audio stream found." << endl;
    }

    // 6. �ٴμ�飬���������ʼ������������ʧЧ�ˣ�ҲӦ�����˳�
    if (videoStreamIndex < 0 && audioStreamIndex < 0) {
        cerr << "MediaPlayer Error: Failed to initialize any valid decoders." << endl;
        m_demuxer->close();
        return -1;
    }

    // 7. ��������Ƶ����׼����Ƶ�����Դ
    if (videoStreamIndex >= 0) {
        cout << "MediaPlayer: Preparing video-specific resources (SWS context etc.)." << endl;
        // ���Ѿ���ʼ���Ľ�������ȡά�Ⱥ����ظ�ʽ
        int dec_width = m_videoDecoder->getWidth();
        int dec_height = m_videoDecoder->getHeight();

        if (dec_width <= 0 || dec_height <= 0) {
            cerr << "MediaPlayer Error: Decoder returned invalid dimensions (" << dec_width << "x" << dec_height << ")." << endl;
            m_demuxer->close();
            return -1;  // ��Ƶ��ʼ������
        }
        // (δ������Ҫ)���ڴ���� SWS Context �ĳ�ʼ������
    }

    cout << "MediaPlayer: FFmpeg demuxer and decoders initialization process finished." << endl;
    return 0;
}


int MediaPlayer::handle_event(const SDL_Event& event) {
    switch (event.type) {
    // �رհ�ť
    case SDL_QUIT:
        cout << "MediaPlayer: SDL_QUIT event received, requesting quit." << endl;
        m_quit = true;
        return 0;   //��ʾ�˳�

    case SDL_KEYDOWN:
        // ESC�˳�
        if (event.key.keysym.sym == SDLK_ESCAPE) {
            cout << "MediaPlayer: Escape key pressed, requesting quit." << endl;
            m_quit = true;
            return 0;
        }
        // �ո����ͣ
        if (event.key.keysym.sym == SDLK_SPACE) {
            if (m_pause) {                  // ����ͣ������
                m_clockManager->resume();   // �ڲ����� SDL_PauseAudioDevice(..., 0)
                m_pause = false;            // �����ڲ�m_pause��־ͬ��
                cout << "MediaPlayer: Resumed." << endl;
                m_pause_cond.notify_all();  // ֪ͨ���еȴ��� m_pause_cond �ϵ��̣߳����Լ�����
            }
            else {                          // �Ӳ��ŵ���ͣ
                m_clockManager->pause();    // �ڲ����� SDL_PauseAudioDevice(..., 1)
                m_pause = true;             // �����ڲ�m_pause��־ͬ��
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

            // ֪ͨ��Ⱦ�������ڴ�С����
            if (m_videoRenderer) {
                if (!m_videoRenderer->onWindowResize(newWidth, newHeight)) {
                    cerr << "MediaPlayer: Failed to handle window resize." << endl;
                }
                m_videoRenderer->refresh(); // ����ˢ����ʾ
            }
        }
        // ���ڻָ��¼�����
        else if (event.window.event == SDL_WINDOWEVENT_RESTORED) {
            cout << "MediaPlayer: Window restored, refreshing display." << endl;
            if (m_videoRenderer) {
                m_videoRenderer->refresh();
            }
        }
        // ���ڻ�ý����¼�����  
        else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            cout << "MediaPlayer: Window focus gained, refreshing display." << endl;
            if (m_videoRenderer) {
                m_videoRenderer->refresh();
            }
        }
        // ������ʾ�¼�����
        else if (event.window.event == SDL_WINDOWEVENT_SHOWN) {
            cout << "MediaPlayer: Window shown, refreshing display." << endl;
            if (m_videoRenderer) {
                m_videoRenderer->refresh();
            }
        }
        // ���ڱ�¶�¼�����������Ҫ�ػ�ʱ��
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

    case REFRESH_EVENT: // �����ʹ����ѯ/�ӳ�ѭ���������ʹ�ô��¼���������Ⱦ
        break;

    case BREAK_EVENT:// �����Ҫ���� SDL_Delay �����߳�
        cout << "MediaPlayer: Break event received." << endl;
        break;

    default:
        break;
    }
    return 1; // ��ʾ����
}

// ������ѭ����������
int MediaPlayer::runMainLoop() {
    cout << "MediaPlayer: Starting main loop (event handling and video render trigger)." << endl;

    // ֻ������Ƶ��ʱ��������Ƶ��Ⱦ�߳�
    if (videoStreamIndex >= 0) {
        // ������Ƶ��Ⱦ�߳� (��ѭ��ǰ����)
        // ��Ƶ��Ⱦ�߳��ڴ������������ǹ���׶Σ�����Ϊ�˽������󹹽�/����׼�����롰��������/�û�����������
        m_videoRenderthread = SDL_CreateThread(video_render_thread_entry, "VideoRenderThread", this);
        if (!m_videoRenderthread) {
            cerr << "MediaPlayer Error: Could not create video render thread." << endl;
            m_quit = true; // �������̷߳����˳��ź�
            // ȷ���ڷ��ش���֮ǰ�����߳��Ѽ���
            if (m_videoDecodeThread) SDL_WaitThread(m_videoDecodeThread, nullptr);
            if (m_demuxThread) SDL_WaitThread(m_demuxThread, nullptr);
            return -1;
        }
    }

    SDL_Event event;
    Uint64 last_window_check = SDL_GetTicks64();
    // ���Ӽ��������Ϊ��Ⱦ�߳�Ҳ�ڼ�飬�˴�ֻ�Ǳ���
    const Uint64 WINDOW_CHECK_INTERVAL = 1000;      // ÿ����һ�δ���״̬

    while (!m_quit) {
        // ���� SDL �¼�
        // ʹ�� SDL_WaitEventTimeout ����CPU��ת
        if (SDL_WaitEventTimeout(&event, 100)) {    // �ȴ���� 100 ������¼�
            if (handle_event(event) == 0) {         // handle_event ���� 0 �˳�
                m_quit = true;                      // ��� handle_event �����˳�����ȷ������ m_quit
            }
        }

        // ���ڼ�鴰��״̬����ʹû���¼�������Ϊ����ˢ�»���
        Uint64 current_time = SDL_GetTicks64();
        if (current_time - last_window_check > WINDOW_CHECK_INTERVAL) {
            if (m_videoRenderer && !m_pause) { // ��ͣʱ����Ҫ��ѭ������ˢ��
                // ����ˢ����ʾ��ȷ����û�н���ʱҲ��������ʾ
                //cout << "MediaPlayer MainLoop: Periodic video render check." << endl;
                m_videoRenderer->refresh();
            }
            last_window_check = current_time;
        }
        // �ٴμ�� m_quit �Է�������һ���߳����û��߷�����ʱ
        if (m_quit) break;
    }

    cout << "MediaPlayer: Main loop requested to quit." << endl;
    // m_videoRenderthread �������������б�join��
    // �����̣߳�demux��video_decode��Ҳ���� m_quit ���˳���
    // ���ǵ�joinҲ�����������д���
    return 0;
}

void MediaPlayer::cleanup_ffmpeg_resources() {
    cout << "MediaPlayer: Cleaning up FFmpeg resources..." << endl;

    // 1. ������������ʽ�ͷ�FFmpeg�������ģ�������ͨ�� unique_ptr �������������������
    if (m_videoDecoder) {
        m_videoDecoder.reset(); // .reset()����������������������������close()
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

    // 2. �����ֶ������FFmpeg��ָ���Ա
    if (m_decodingVideoPacket) {
        // av_frame_free ���Զ�ִ�� unref
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

    // 1�������˳��ź�
    m_quit = true;
    // ֪ͨ�������ͷ���
    {
        std::lock_guard<std::mutex> lock(m_pause_mutex);
        m_pause_cond.notify_all(); // ���ѿ�������ͣ�еȴ����߳�
    }
    // ���ѵȴ����е��߳�
    if (m_videoPacketQueue) m_videoPacketQueue->signal_eof();
    if (m_audioPacketQueue) m_audioPacketQueue->signal_eof();
    if (m_videoFrameQueue) m_videoFrameQueue->signal_eof();
    if (m_audioFrameQueue) m_audioFrameQueue->signal_eof();

    // 2���ȴ������߳̽���
    // �ȵȴ��������̣߳��ٵȴ��������̣߳�����Ǳ������
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
    // �߳���ȫ��ֹͣ�����߼�˳���ͷ�������Դ

    // 3. �ͷ�SDL��Ⱦ��
    // ����Texture, Renderer, Window
    // SDL��Ⱦ������FFmpeg��Ϣ����������FFmpeg����
    if (m_audioRenderer) {
        m_audioRenderer.reset();
        cout << "MediaPlayer: Audio Renderer cleaned up." << endl;
    }
    if (m_videoRenderer) {
        m_videoRenderer.reset();
        cout << "MediaPlayer: Video Renderer cleaned up." << endl;
    }

    // 4. �ͷ�FFmpeg������Դ
    cleanup_ffmpeg_resources();

    // �ͷŶ��к�ʱ��
    // �����п��ܻ����� AVPacket/AVFrame�����ǵ��������� FFmpeg ��
    // ������ cleanup_ffmpeg_resources ֮�����
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

// �⸴���߳���ں�������
int MediaPlayer::demux_thread_entry(void* opaque) {
    // ��ȡMediaPlayerʵ��ָ��
    return static_cast<MediaPlayer*>(opaque)->demux_thread_func();
}

int MediaPlayer::demux_thread_func() {
    cout << "MediaPlayer: Demux thread started." << endl;
    AVPacket* demux_packet = av_packet_alloc();//���ذ������ڴӽ⸴������ȡ
    if (!demux_packet) {
        cerr << "MediaPlayer DemuxThread Error: Could not allocate demux_packet." << endl;
        if (m_videoPacketQueue) { m_videoPacketQueue->signal_eof(); } // �� ���� ��ΪEOF ���д���
        if (m_audioPacketQueue) { m_audioPacketQueue->signal_eof(); }
        m_quit = true;  // ���ش����������������˳�
        return -1;
    }

    int read_ret = 0;
    while (!m_quit) {
        // ��ͣ�����߼�
        // ʹ����������ȷ�� �ȴ� wait ������ִ�к�ʱ����ǰ�ͷ��� m_pause_mutex
        {
            std::unique_lock<std::mutex> lock(m_pause_mutex);
            // wait������������� m_pause Ϊ true �� m_quit Ϊ false���̻߳��ڴ�����
            // ֱ�� m_pause_cond.notify_all() �����ã����Ż��������¼������
            m_pause_cond.wait(lock, [this] { return !m_pause || m_quit; });
        }
        // ��������˳��������ѣ���ֱ���˳�ѭ��
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
                m_quit = true; // ���ش���
            }
            break;//�˳��⸴��ѭ��
        }

        if (demux_packet->stream_index == videoStreamIndex) {
            if (m_videoPacketQueue) {
                if (!m_videoPacketQueue->push(demux_packet)) {
                    // Push ����ʧ�ܣ����� ��������������Ϊ������/���� �� �ѷ��� EOF �źţ�
                    // PacketQueue �� push �ڶ�������ʱ��¼���󲢶������ݰ�
                    // �����ݰ������·�ȡ�����ã�������ﲻ�ö��⴦��
                }
            }
        }
        else if (audioStreamIndex >= 0 && demux_packet->stream_index == audioStreamIndex) {
            if (m_audioPacketQueue) {
                if (!m_audioPacketQueue->push(demux_packet)) {
                    // ����ʧ�ܣ������������˴�����������
                }
            }
        }
        // else {
            // ���������������ݰ�����ʱ����
        // }

        // PacketQueue::push ����� av_packet_ref���������⸴���̶߳�ȡ��ԭʼ����Ҫ unref
        av_packet_unref(demux_packet);
    }

    av_packet_free(&demux_packet);//�ͷű��ذ�
    cout << "MediaPlayer: Demux thread finished." << endl;

    // ȷ����ʹѭ���� m_quit �˳���EOFҲ�ᷢ��
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

// ��Ƶ�����߳���ں�������
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

    AVFrame* decoded_frame = nullptr; // ���� m_videoDecoder->decode() ����

    while (!m_quit) {
        // ��ͣ�����߼�
        {
            std::unique_lock<std::mutex> lock(m_pause_mutex);
            m_pause_cond.wait(lock, [this] { return !m_pause || m_quit; });
        }
        if (m_quit) break;

        if (!m_videoPacketQueue->pop(m_decodingVideoPacket, 100)) {
            if (m_videoPacketQueue->is_eof()) {
                cout << "MediaPlayer VideoDecodeThread: Packet queue EOF, starting to flush decoder." << endl;
                int flush_ret = m_videoDecoder->decode(nullptr, &decoded_frame); // ���� nullptr ����ϴ
                while (flush_ret == 0) {
                    if (decoded_frame) {
                        if (!m_videoFrameQueue->push(decoded_frame)) { // FrameQueue::push �� ref
                            cerr << "MediaPlayer VideoDecodeThread: Failed to push flushed frame to frame queue." << endl;
                        }
                        av_frame_free(&decoded_frame); // �ͷ� decode() ����� shell
                        // decoded_frame ������Ч
                    }
                    flush_ret = m_videoDecoder->decode(nullptr, &decoded_frame); // ���Ի�ȡ����
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
                if (!m_videoFrameQueue->push(decoded_frame)) { // FrameQueue::push �� ref
                    cerr << "MediaPlayer VideoDecodeThread: Failed to push decoded frame to frame queue." << endl;
                }
                av_frame_free(&decoded_frame); // <--- �ͷ� decode() ����� shell
                // decoded_frame ������Ч
            }
        }
        else if (decode_ret == AVERROR(EAGAIN)) {
            // ����ѭ�������Է�����һ���������֡
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

// ��Ƶ��Ⱦ�߳���ں�������
int MediaPlayer::video_render_thread_entry(void* opaque) {
    return static_cast<MediaPlayer*>(opaque)->video_render_func();
}

int MediaPlayer::video_render_func() {
    cout << "MediaPlayer: VideoRenderThread started." << endl;
    if (!m_renderingVideoFrame) { // ȷ�� shell �ѷ���
        cerr << "MediaPlayer VideoRenderThread Error: m_renderingVideoFrame is null." << endl;
        return -1;
    }

    // ״̬���ٱ���
    Uint64 last_refresh_time = SDL_GetTicks64();
    const Uint64 FORCE_REFRESH_INTERVAL = 500;  // ǿ��ˢ�¼��(ms)

    while (!m_quit) {
        // ��ͣ�����߼�
        {
            std::unique_lock<std::mutex> lock(m_pause_mutex);
            m_pause_cond.wait(lock, [this] { return !m_pause || m_quit; });
        }
        if (m_quit) break;

        // ���ԴӶ��л�ȡ��֡��ʹ�ô���ʱ��pop����ֹ��������
        bool got_new_frame = m_videoFrameQueue->pop(m_renderingVideoFrame, 100);

        if (got_new_frame) {
            // ����֡��������Ⱦ
            if (!m_videoRenderer->renderFrame(m_renderingVideoFrame)) {
                cerr << "MediaPlayer VideoRenderThread: renderFrame failed." << endl;
                m_quit = true; // ��Ⱦ�����˳�����
            }
            frame_cnt++;
            av_frame_unref(m_renderingVideoFrame);  // ʹ�ú� unref �����ã��Ա� shell ����
            last_refresh_time = SDL_GetTicks64();   // �ɹ���Ⱦ�����ˢ��ʱ��
        }
        else {
            // ��ȡ֡ʧ�� (��ʱ��EOF)
            if (m_videoFrameQueue->is_eof()) {
                cout << "MediaPlayer VideoRenderThread: Frame queue EOF and empty. Exiting." << endl;
                break;
            }

            // û����֡ʱ������Ƿ���Ҫǿ��ˢ��
            Uint64 current_time = SDL_GetTicks64();
            bool should_force_refresh = false;

            // 1. ����ʱ��Ķ���ˢ��
            if (current_time - last_refresh_time > FORCE_REFRESH_INTERVAL) {
                should_force_refresh = true;
            }

            // 2. ���ڴ���״̬���������
            if (m_videoRenderer) {
                // ����ת���Ե������������еķ���
                // dynamic_cast �ڳ�������ʱ��������ת��������ִ�а�ȫ������ת�ͣ������ࣩ
                auto* sdl_renderer = dynamic_cast<SDLVideoRenderer*>(m_videoRenderer.get());
                if (sdl_renderer) {
                    SDL_Window* window = sdl_renderer->getWindow();
                    if (window) {
                        Uint32 flags = SDL_GetWindowFlags(window);
                        // ������ڿɼ���δ��С������ֵ��ˢ��
                        if ((flags & SDL_WINDOW_SHOWN) && !(flags & SDL_WINDOW_MINIMIZED)) {
                            // ����������Դ���ˢ�£�����ʱ����δ��
                        }
                        else {
                            // ������ڱ����ػ���С������û��Ҫˢ��
                            should_force_refresh = false;
                        }
                    }
                }
            }
            // û����֡����Ҫǿ��ˢ��
            if (should_force_refresh && m_videoRenderer) {
                cout << "MediaPlayer: Force refreshing display (no new frame)." << endl;
                m_videoRenderer->refresh();
                last_refresh_time = current_time; // ˢ�º����ʱ��
            }
        }
    }
    cout << "MediaPlayer: Video render thread finished. Total frames rendered: " << get_frame_cnt() << endl;
    return 0;
}

// ��Ƶ�����߳���ں�������
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

    AVFrame* decoded_frame = nullptr; // ���� m_audioDecoder->decode() ����

    while (!m_quit) {
        // ��ͣ�����߼�
        {
            std::unique_lock<std::mutex> lock(m_pause_mutex);
            m_pause_cond.wait(lock, [this] { return !m_pause || m_quit; });
        }
        if (m_quit) break;

        // 1. ����Ƶ��������ȡ��һ����
        if (!m_audioPacketQueue->pop(m_decodingAudioPacket, 100)) {
            // ���popʧ�ܣ�����Ƿ�������EOF
            if (m_audioPacketQueue->is_eof()) {
                cout << "MediaPlayer AudioDecodeThread: Packet queue EOF, starting to flush decoder." << endl;

                // ���� nullptr ��ˢ�½�����
                int flush_ret = m_audioDecoder->decode(nullptr, &decoded_frame);
                while (flush_ret == 0) { // ������ȡֱ֡���������޸������
                    if (decoded_frame) {
                        if (!m_audioFrameQueue->push(decoded_frame)) { // FrameQueue::push �� ref
                            cerr << "MediaPlayer AudioDecodeThread: Failed to push flushed frame to frame queue." << endl;
                        }
                        // decode() �������µ�֡�������˾ɵģ������ͷ������(shell)
                        av_frame_free(&decoded_frame);
                    }
                    // ���Ի�ȡ��һ����ϴ֡
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

                m_audioFrameQueue->signal_eof(); // ����Ƶ֡���з���EOF�ź�
                break; // �˳�ѭ��
            }
            else {
                // pop��ʱ������ѭ��
                continue;
            }
        }

        // 2. �������ݰ�
        int decode_ret = m_audioDecoder->decode(m_decodingAudioPacket, &decoded_frame);
        av_packet_unref(m_decodingAudioPacket); // ���������Ҫ�����ݰ�

        if (decode_ret == 0) {
            if (decoded_frame) {
                if (!m_audioFrameQueue->push(decoded_frame)) {
                    cerr << "MediaPlayer AudioDecodeThread: Failed to push decoded frame to frame queue." << endl;
                }
                // �ͷ�֡���
                av_frame_free(&decoded_frame);
            }
        }
        else if (decode_ret == AVERROR(EAGAIN)) {
            // ��������Ҫ�������룬����ѭ���Ի�ȡ��һ����
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
            // �������ش��󣬷���EOF�źŲ��˳�
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

// ��Ƶ��Ⱦ�߳���ں�������
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
        // ��ͣ�����߼�
        {
            std::unique_lock<std::mutex> lock(m_pause_mutex);
            m_pause_cond.wait(lock, [this] { return !m_pause || m_quit; });
        }
        if (m_quit) break;

        // ����Ƶ֡������ȡ��һ֡������100ms��ʱ
        if (!m_audioFrameQueue->pop(m_renderingAudioFrame, 100)) {
            if (m_audioFrameQueue->is_eof()) {
                cout << "MediaPlayer AudioRenderThread: Frame queue EOF and empty. Exiting." << endl;
                break;
            }
            continue; // ��ʱ������ѭ������˳�����ͣ״̬
        }

        // ������Ⱦ����������һ֡
        if (m_audioRenderer && !m_audioRenderer->renderFrame(m_renderingAudioFrame, m_quit)) {
            // ��� renderFrame ��Ϊ�˳������������������� false����׼���˳��߳�
            if (!m_quit) {
                cerr << "MediaPlayer AudioRenderThread: renderFrame failed." << endl;
                m_quit = true; // ��Ⱦ������ֹ����
            }
        }

        // �ͷŶ�֡���ݵ����ã��Ա� m_renderingAudioFrame ���Ա�����
        av_frame_unref(m_renderingAudioFrame);
    }
    cout << "MediaPlayer: Audio render thread finished." << endl;
    return 0;
}
