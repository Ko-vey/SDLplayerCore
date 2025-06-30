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

    m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
        width, height);
    if (!m_texture) {
        std::cerr << "Texture could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        return false;
    }

    m_yuv_frame = av_frame_alloc();
    if (!m_yuv_frame) {
        std::cerr << "Could not allocate YUV frame" << std::endl;
        return false;
    }
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(m_yuv_frame->data, m_yuv_frame->linesize, buffer, AV_PIX_FMT_YUV420P, width, height, 1);

    m_sws_context = sws_getContext(width, height, decoderPixelFormat,
        width, height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws_context) {
        std::cerr << "Could not create SwsContext" << std::endl;
        return false;
    }

    m_last_rendered_frame = av_frame_alloc();
    if (!m_last_rendered_frame) {
        std::cerr << "Could not allocate last rendered frame" << std::endl;
        return false;
    }

    m_clock_manager = clockManager;
    // ������Ƶ��Ⱥ͸߶�
    m_video_width = width;
    m_video_height = height;
    // ��¼��ʼ���ڴ�С
    m_window_width = width;
    m_window_height = height;

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
    m_clock_manager->setVideoClock(pts, duration);

    // ������Ƶʱ������ʱ�ӵĲ�ֵ������ʱ��
    double delay = pts - m_clock_manager->getMasterClockTime();

    // ������ʱ����ͬ������
    const double AV_SYNC_THRESHOLD_MIN = 0.04; // ͬ����ֵ���� (40ms)
    const double AV_SYNC_THRESHOLD_MAX = 0.1;  // ͬ����ֵ���� (100ms)
    const double AV_NOSYNC_THRESHOLD = 10.0;   // ��ͬ�������ã���ֵ (10s)

    // �����ʱ�Ƿ���󣬹�������Ϊʱ�Ӳ�ͬ����������ʱ
    if (delay > AV_NOSYNC_THRESHOLD || delay < -AV_NOSYNC_THRESHOLD) {
        // ʱ�Ӳ����󣬿��ܳ����ˣ�������Ƶʱ��
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
    // ����Ⱦǰ��ȡ���µĴ��ڳߴ�
    int currentWindowWidth, currentWindowHeight;
    SDL_GetWindowSize(m_window, &currentWindowWidth, &currentWindowHeight);

    // ������������������Ⱦ��Դ
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_renderer || !m_texture) return false;

    // ����Ⱦǰ���Ƚ���ǰ֡���ݿ�¡�� m_last_rendered_frame
    // ���� refresh() �����������µ���Ч֡����
    av_frame_unref(m_last_rendered_frame); // �ͷžɵ�����
    if (av_frame_ref(m_last_rendered_frame, frame) < 0) {
        std::cerr << "SDLVideoRenderer: Failed to reference last frame." << std::endl;
        // ���������󣬿��Լ���
    }

    // ������ڳߴ緢���仯����Ҫ���¼�¼�Ĵ��ڴ�С�������ؽ���Դ���˴������¼�¼��
    m_window_width = currentWindowWidth;
    m_window_height = currentWindowHeight;

    // ɫ�ʿռ�ת�� (raw AVframe -> YUV)
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

    // �����Ⱦ������Ϊ��ɫ
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255); // ��ɫ����
    SDL_RenderClear(m_renderer);

    // ���㱣�ֿ�߱ȵ���ʾ����
    SDL_Rect displayRect = calculateDisplayRect(currentWindowWidth, currentWindowHeight);
    
    // �����º�������Ƶ���Ⱦ��
    // ʹ�ü�����ľ��ν�����Ⱦ����������������
    SDL_RenderCopy(m_renderer, m_texture, nullptr, &displayRect);

    // ��ʾ��Ⱦ���
    SDL_RenderPresent(m_renderer);
    m_texture_lost = false; // �ɹ���Ⱦ�󣬱����������Ч��

    // --- 3������״̬ ---
    // ������һ֡����Ϣ��������һ��ѭ���Ĺ���
    m_frame_last_pts = pts;
    m_frame_last_duration = duration;

    return true;
}

// ˢ������
void SDLVideoRenderer::refresh() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_renderer || !m_window) return;

    // ���û����Ч�����һ֡����ֻ����
    if (!m_last_rendered_frame || m_last_rendered_frame->width == 0) {
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
        SDL_RenderClear(m_renderer);
        SDL_RenderPresent(m_renderer);
        return;
    }

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

    // ����SDL��Ⱦ�����߼���С������Զ���������
    // ����Ȼ��Ҫ�ֶ�������ʾ���������ֿ�߱�

    std::cout << "SDLVideoRenderer: Window resized to " << newWidth << "x" << newHeight << std::endl;
    return true;
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
