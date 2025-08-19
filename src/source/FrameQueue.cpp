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

#include "../include/FrameQueue.h"
#include <iostream>

FrameQueue::FrameQueue(size_t max_queue_size):max_size(max_queue_size) {}

FrameQueue::~FrameQueue() { 
	clear(); 
}

bool FrameQueue::push(AVFrame* frame) {
	if (!frame) {
		cerr << "FrameQueue::push: Input frame is null." << endl;
		return false;
	}

	AVFrame* frame_clone = av_frame_alloc();
	if (!frame_clone) {
		cerr << "FrameQueue::push: av_frame_alloc failed." << endl;
		return false;
	}

	// Ϊ����frame�����ݴ���һ���µ����ã�����frame_clone����
	int ret = av_frame_ref(frame_clone, frame);
	if (ret < 0) {
		cerr << "FrameQueue::push: av_frame_ref failed with error " << ret << endl;
		av_frame_free(&frame_clone); // �ͷŸշ����frame_clone�ṹ
		return false;
	}

	std::unique_lock<std::mutex> lock(mutex);
	
	// ���������������������Ҷ�����������ȴ�
	// ����ʹ��whileѭ������ֹ����ٻ��ѡ�
	while (max_size > 0 && queue.size() >= max_size && !eof_signaled) {
		//cerr << "FrameQueue::push: Queue is full. Holding frame and wait." << endl;
		cond_producer.wait(lock);
	}

	// ����ڵȴ��ڼ䱻֪ͨEOF����������
	if (eof_signaled) {
		return false;
	}

	queue.push(frame_clone);

	lock.unlock(); // ��֪֮ͨǰ�������ͷ���
	// ֪ͨһ�����ڵȴ��������ߣ�����������������
	cond_consumer.notify_one();

	return true;
}

bool FrameQueue::pop(AVFrame* frame, int timeout_ms) {
	if (!frame) {
		cerr << "FrameQueue::pop: Output frame parameter is null." << endl;
		return false;
	}

	std::unique_lock<std::mutex> lock(mutex);

	// ������Ϊ����δ�յ�EOF�ź�ʱ�ȴ�
	while (queue.empty() && !eof_signaled) {
		if (timeout_ms == 0) { // ������
			return false;
		}
		if (timeout_ms < 0) { // ���޵ȴ�
			cond_consumer.wait(lock);
		}
		else {
			// �ж��̵߳Ļ����Ƿ�����Ϊ��ʱ
			if (cond_consumer.wait_for(lock, std::chrono::milliseconds(timeout_ms)) 
				== std::cv_status::timeout) {
				return false; // �ȴ���ʱ
			}
		}
	}

	// �������Ϊ�������յ�EOF�źţ�����Ϊ������
	if (queue.empty() && eof_signaled) {
		return false;
	}

	// �Ӷ�����ȡ��һ��֡
	AVFrame* src_frame = queue.front();
	queue.pop();
	lock.unlock(); // ��ִ��FFmpeg����ǰ�����ͷ���

	// unref�ɵģ�ref�µ�
	av_frame_unref(frame);
	int ret = av_frame_ref(frame, src_frame);
	if (ret < 0) {
		cerr << "FrameQueue::pop: av_frame_ref failed to copy to output frame. Error: " << ret << endl;
		// ��ʹ����ʧ�ܣ�src_frame Ҳ���뱻��ȷ����
		// ��ʱ�û��ṩ�� frame ���ܴ��ڲ�ȷ��״̬���������ͷ� src_frame
		av_frame_free(&src_frame);
		// ���� false����Ϊ����δ�ܳɹ����ݸ�������
		return false;
	}

	// �ͷ� src_frame�������� push ʱ���䣬�������������ⲿ frame ���ã�
	av_frame_free(&src_frame); // �ͷſ������Լ�����ĸ���������

	// ֪ͨһ�������ڵȴ��������ߣ��������пռ���
	cond_producer.notify_one();

	return true;
}

size_t FrameQueue::size() const {
	std::lock_guard<std::mutex> lock(mutex);
	return queue.size();
}

void FrameQueue::clear() {
	std::unique_lock<std::mutex> lock(mutex);
	while (!queue.empty()) {
		AVFrame* frm = queue.front();
		queue.pop();
		av_frame_free(&frm);
	}
	// eof_signaled ״̬ͨ����clearʱ���ı䣬
	// ��Ϊclear�������м������EOF�������Ľ�����
	// �����Ҫ��ȫ���ã��������һ��reset()������
}

void FrameQueue::signal_eof() {
	std::unique_lock<std::mutex> lock(mutex);
	eof_signaled = true;
	lock.unlock();
	// �������еȴ��������ߺ������ߣ��������ܹ����eof_signaled��־���˳�
	cond_consumer.notify_all();// ֪ͨ���еȴ����������߳�EOF״̬�Ѹı�
	cond_producer.notify_all();// ֪ͨ���еȴ����������߳�
}

bool FrameQueue::is_eof() const {
	std::lock_guard<std::mutex> lock(mutex);
	return eof_signaled && queue.empty();
}
