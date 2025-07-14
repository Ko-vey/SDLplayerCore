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

#include "IVideoDecoder.h"

// ǰ������ FFmpeg ���ͣ�������ͷ�ļ��а����Ӵ��FFmpegͷ�ļ�
struct AVCodecContext;

class FFmpegVideoDecoder : public IVideoDecoder {
public:
	FFmpegVideoDecoder();
	~FFmpegVideoDecoder() override;

	//��ֹ�������캯���͸�ֵ������
	FFmpegVideoDecoder(const FFmpegVideoDecoder&) = delete;
	FFmpegVideoDecoder& operator=(const FFmpegVideoDecoder&) = delete;

	/**
	* @brief ʹ�ø����ı������������ʱ�ӹ����� ��ʼ����Ƶ��������
	* @param codecParams ָ��ӽ⸴������ȡ����Ƶ���� AVCodecParameters ��ָ�롣
	* @return ����ʼ���ɹ����� true�����򷵻� false��
	*/
	bool init(AVCodecParameters* codecParams) override;

	/**
	* @brief ��������Ƶ������Ϊһ��������Ƶ֡��
	* �����߸������ packet ���������ڡ�
	* �������ɹ������������һ���µ� AVFrame ��ͨ�� frame �������ء��������ڴ������ frame ��
	* ������� av_frame_free() ���ͷ�����
	* @param packet �����������ѹ����Ƶ���ݵ� AVPacket��
	* ��Ϊ nullptr�����ʾ��ϴ����������ȡ�ڲ������֡����
	* @param frame ָ�� AVFrame ָ���ָ�롣�������ɹ���*frame ��ָ���·���Ľ���֡��
	* �������ʧ�ܻ���Ҫ�������룬*frame������Ϊ nullptr��
	* @return �ɹ�ʱ���� 0��֡�ѽ��벢���� *frame����
	* ����Ҫ���������򷵻� AVERROR(EAGAIN)����ʱ *frame Ϊnullptr
	* ��������ĩβ�򷵻� AVERROR_EOF����ʱ *frame Ϊ nullptr����
	* ������ֵ��ʾ�������
	*/
	int decode(AVPacket* packet, AVFrame** frame) override;

	/**
	* @brief �رս��������ͷ�������� FFmpeg ��Դ��
	* ���ô˷����󣬽�����ʵ���������ã������ٴε��� init()��
	*/
	void close() override;

	/**
	* @brief ��ȡ��������Ƶ֡�Ŀ�ȡ�
	* @return ��ȣ���λ�����أ������������δ��ʼ���򷵻� 0��
	*/
	int getWidth() const override;
	
	/**
	* @brief ��ȡ��������Ƶ֡�ĸ߶ȡ�
	* @return �߶ȣ���λ�����أ������������δ��ʼ���򷵻� 0��
	*/
	int getHeight() const override;

	/**
	* @brief ��ȡ��������Ƶ֡�����ظ�ʽ��
	* @return AVPixelFormat ö��ֵ�����������δ��ʼ���򷵻� AV_PIX_FMT_NONE��
	*/
	AVPixelFormat getPixelFormat() const override;

	/**
	* @brief ��ȡ��Ƶ����ʱ���׼��
	* @return ��ʾʱ���׼�� AVRational �ṹ�壬���������δ��ʼ���򷵻�{0,1}��
	*/
	AVRational getTimeBase() const override;
	
	/**
	* @brief ��ȡ��Ƶ����ƽ��֡�ʡ�
	* @return ��ʾƽ��֡�ʵ� AVRational �ṹ�壬���������δ��ʼ���򷵻�{0,1}��
	*/
	AVRational getFrameRate() const override;

private:
	AVCodecContext* m_codecContext;	// FFmpeg ������������
};
