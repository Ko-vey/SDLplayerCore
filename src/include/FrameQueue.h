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

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>//����ʱ�� std::chrono::milliseconds

//AVPacket �� AVFrame
extern "C" {
#include <libavcodec/avcodec.h>
}

using namespace std;

class FrameQueue {
private:
	std::queue<AVFrame*> queue;
	mutable std::mutex mutex;				// �ɱ����ʡ��޸ĵ��������ͣ�mutable ������const������lock
	std::condition_variable cond_consumer;	// ������Ϊ��ʱ�������ߵȴ�
	std::condition_variable cond_producer;	// ����������ʱ�������ߵȴ�
	bool eof_signaled = false;
	// size_t - �޷����������ͣ�����������ƽ̨����
	size_t max_size = 0;		// 0��ʾ�����ƣ�>0��ʾ�����������

public:
	FrameQueue(size_t max_queue_size = 0);	//���캯������ָ�������д�С
	~FrameQueue();							//����������������Դ

	/**
	* @brief �������֡������β����
	* �ڲ���Ϊframe����һ���µ����ø������д洢��
	* ������Ϊ����Ϊ��ʱ��������������
	* @return true - �ɹ���false - ʧ�ܣ����������������frameΪnull��
	*/
	bool push(AVFrame* frame);

	/**
	* @brief �Ӷ���ͷ����ȡ֡�����ݻ����õ��������ṩ��frame��
	* @param frame: �������ṩ��AVFrameָ�룬���ڽ������ݡ�����ǰӦȷ�����ѷ���(av_frame_alloc)
	* ����֮ǰ���õ�����unref������������unref����ref
	* @param timeout_ms���ȴ���ʱʱ�䣨���룩
	* <0:���޵ȴ�
	* 0��������
	* >0���ȴ�ָ��ʱ��
	* @return �ɹ���ȡframe����true��ʧ���򷵻�false����ʱ������Ϊ����EOF�������Ϊ�յķ��������ã�
	*/
	bool pop(AVFrame* frame, int timeout_ms = -1);

	/**
	* @brief ��ȡ���е�ǰԪ������
	*/
	size_t size() const;

	/**
	* @brief ��ն����е��������ݰ������ͷ�����Դ
	*/
	void clear();

	/**
	* @brief ֪ͨ����������������EOF�����ỽ�����еȴ�pop������ �����еȴ���
	*/
	void signal_eof();

	/**
	* @brief ����Ƿ���֪ͨEOF�Ҷ����ѿ�
	*/
	bool is_eof() const;

	// ��ֹ�������캯���͸�ֵ������
	FrameQueue(const FrameQueue&) = delete;
	FrameQueue& operator=(const FrameQueue&) = delete;
};