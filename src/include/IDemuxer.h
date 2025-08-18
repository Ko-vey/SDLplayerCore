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

struct AVFormatContext;
struct AVPacket;
struct AVCodecParameters;
struct AVRational;
enum AVMediaType;	// enum

class IDemuxer {
public:
	// Ϊ�����������ṩĬ�ϵı�׼ʵ��
	virtual ~IDemuxer() = default;

	/**
	 * @brief ��ý��Դ���ļ�·����
	 * @param 
	 * @return �ɹ�����true��ʧ�ܷ���false
	 */
	virtual bool open(const char* url) = 0;

	/**
	 * @brief �ر�ý��Դ���ͷ���Դ
	 */
	virtual void close() = 0;

	/**
	 * @brief ��ȡ��һ�����ݰ�
	 * @param packet �������ṩ�� AVPacket �ṹ��ָ�룬���ڽ�������
	 * @return �ɹ�����0���ļ���������AVERROR_EOF�����������򷵻�<0������
	 */
	virtual int readPacket(AVPacket* packet) = 0;

	/**
	 * @brief ��ȡ�ײ�� AVFormatContext�����ڻ�ȡ����ϸ��Ϣ��
	 * ע�⣺ͨ��Ӧ������¶�ײ������ģ�����ʱ�б�Ҫ������FFmpeg�������
	 * @return ָ�� AVFormatContext ��ָ�롣��δ����Ϊ nullptr
	 */
	virtual AVFormatContext* getFormatContext() const = 0;

	/**
	 * @brief ����ָ��ý�����͵�������
	 * @param type AVMEDIA_TYPE_VIDEO, AVMEDIA_TYPE_AUDIO, etc.
	 * @return ��������>=0��,��δ�ҵ��򷵻� -1
	 */
	virtual int findStream(AVMediaType type) const = 0;

	/**
	 * @brief ��ȡָ�����ı����������
	 * @param streamIndex ��������
	 * @return ָ�� AVCodecParameters ��ָ�룬��������Ч��Ϊ nullptr
	 */
	virtual AVCodecParameters* getCodecParameters(int streamIndex)const = 0;

	/**
	 * @brief ��ȡý���ļ�����ʱ��
	 */
	virtual double getDuration() const = 0;

	/**
	 * @brief ��ȡָ������ʱ��� (time_base).
	 * @param streamIndex ��������.
	 * @return AVRational �ṹ�壬��ʾʱ�������������Ч�򷵻� {0, 1}��
	 */
	virtual AVRational getTimeBase(int streamIndex) const = 0;
	
	//���������ܣ����ȡԪ���ݵȣ�
	//virtual AVDictionary* getMetadata() const = 0;
};
