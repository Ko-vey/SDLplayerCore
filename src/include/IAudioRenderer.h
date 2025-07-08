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