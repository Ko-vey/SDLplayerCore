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

#include "IClockManager.h"	// ʱ�ӹ������ӿڣ���Ƶ�������֡��PTS���ں�������ʱ��

// FFmpeg���͵�ǰ������
struct AVCodecParameters;	// ������������ṹ��
struct AVPacket;			// ���ݰ��ṹ��
struct AVFrame;				// ����֡�ṹ��
enum AVSampleFormat;		// ������ʽö��
struct AVRational;			// �������ṹ�壨����ʱ���׼��

// ���� getChannelLayout()��AVChannelLayout�Ķ���Ƚϸ��ӣ�
// ��С���н׶�ֱ���� getChannels() ���򵥡�
// ��ȷʵ��Ҫ����ȷ���������֣�������Ҫ���� <libavutil/channel_layout.h>
// ����ʹ���� underlying type�²����ͣ�ͨ���� uint64_t)
// Ϊ������������������и�רע����������

class IAudioDecoder {
public:
	virtual ~IAudioDecoder() = default;

	/**
	* @brief ʹ�ø����ı������������ʱ�������ʼ����Ƶ��������
	* @param codecParams ָ����Ƶ���� AVCodecParameters ��ָ�롣
	* @param timeBase ��Ƶ����ʱ�����
	* @param clockManager ָ��ʱ�ӹ�������ָ�롣
	* @return ����ʼ���ɹ����� true�����򷵻� false��
	*/
	virtual bool init(AVCodecParameters* codecParams, AVRational timeBase, IClockManager* clockManager) = 0;

	/**
	* @brief ��������Ƶ������Ϊһ����Ƶ֡��PCM���ݣ���
	* @param packet �����������ѹ����Ƶ���ݵ� AVPacket��
	* ����������ʱ��ȡ��������ʣ���֡��draining the decoder����
	* ͨ���Ƿ���һ���յ�packet��packet->data==nullptr && packet->size==0������������
	* @param frame ָ�� AVFrame ָ���ָ�룬��ָ�뽫��������PCM������䣬
	* @return �ɹ�ʱ����0��֡�ѽ��룩������Ҫ���������򷵻� AVERROR(EAGAIN)��
	* ������������ȫˢ�¡��Ҳ������и���֡����򷵻�AVERROR_EOF��
	* ʧ��ʱ���ظ��Ĵ�����롣
	*/
	virtual int decode(AVPacket* packet, AVFrame** frame) = 0;

	/**
	* @brief �رս��������ͷ����������Դ��
	* ���ô˷����󣬽�����ʵ���������á�
	*/
	virtual void close() = 0;

	/**
	* @brief ��ȡ��������Ƶ�����ʣ���44.1kHz��48kHz����
	* @return �����ʡ�
	*/
	virtual int getSampleRate() const = 0;

	/**
	* @brief ��ȡ��������Ƶ������������ 1-��������2-����������
	* @return ������
	*/
	virtual int getChannels() const = 0;

	/**
	* @brief ��ȡ�������Ƶ�Ĳ�����ʽ���� AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLTP)��
	* �����Ϣ������Ƶ��Ⱦ��������Ƶ�豸�ǳ���Ҫ��
	* @return AVSampleFormat ö��ֵ��
	*/
	virtual enum AVSampleFormat getSampleFormat() const = 0;

	/**
	* @brief ��ȡ��Ƶ����ʱ���׼��time base����
	* ʱ���׼������ʱ����Ļ�����λ��
	* @return ��ʾʱ���׼�� AVRational �ṹ�塣
	*/
	virtual struct AVRational getTimeBase() const = 0;

	// ��ѡ�� ��ȡ�������֣�����ϸ��������Ϣ��
	//virtual uint64_t getChannelLayout() const = 0;

	// ��ȡÿ����������ֽ�����ÿ����������֡���ֽ���
	//��ͨ�����Դ� sample_format �� channels �ƶϳ�������������Ƶ��Ⱦ��������Щ�������㣩
	virtual int getBytesPerSampleFrame() const = 0;
};
