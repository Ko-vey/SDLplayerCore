#pragma once

#include <cstdint>
#include <atomic>
#include "IClockManager.h"

// ǰ������ FFmpeg ����
struct AVFrame;
struct AVRational;
enum AVSampleFormat;

class IAudioRenderer {
public:
    virtual ~IAudioRenderer() = default;

    /**
     * @brief ��ʼ����Ƶ��Ⱦ����
     * @param sampleRate �����Ĳ��Ų����ʡ�
     * @param channels ��������������
     * @param decoderSampleFormat �������ṩ�Ĳ�����ʽ��
     * @param timeBase ��Ƶ����ʱ��������ڼ���PTS��
     * @param clockManager ���ڸ���ʱ���ʱ�ӹ�����ָ�롣
     * @return �ɹ����� true��ʧ�ܷ��� false��
     */
    virtual bool init(int sampleRate, int channels, enum AVSampleFormat decoderSampleFormat,
        AVRational timeBase, IClockManager* clockManager) = 0;

    /**
     * @brief ��Ⱦһ֡��Ƶ��
     * �÷�������б�Ҫ���ز�����������Ƶ�������͵����Ŷ��С�
     * �����Ƶ�������������˷������ܻ�������
     * @param frame Ҫ��Ⱦ����Ƶ֡��
     * @param quit ����ָʾ�˳��̵߳ı�־��
     * @return �ɹ����� true��ʧ�ܷ��� false��
     */
    virtual bool renderFrame(AVFrame* frame, const std::atomic<bool>& quit) = 0;

    /**
     * @brief ��ʼ��ָ���Ƶ���š�
     */
    virtual void play() = 0;

    /**
     * @brief ��ͣ��Ƶ���š�
     */
    virtual void pause() = 0;

    /**
     * @brief ����������Ŷӵ���Ƶ���ݡ���Seek����������Ҫ��
     */
    virtual void flushBuffers() = 0;

    /**
     * @brief �ر���Ƶ��Ⱦ�����ͷ����������Դ��
     */
    virtual void close() = 0;
};