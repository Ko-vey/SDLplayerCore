/*
 * SDLplayerCore - An audio and video player.
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

using namespace std;

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

	// 为输入frame的数据创建一个新的引用，并由frame_clone持有
	int ret = av_frame_ref(frame_clone, frame);
	if (ret < 0) {
		cerr << "FrameQueue::push: av_frame_ref failed with error " << ret << endl;
		av_frame_free(&frame_clone);
		return false;
	}

	std::unique_lock<std::mutex> lock(mutex);
	
	// 当队列满时，只要没有收到 abort 请求，就继续等待
	while (max_size > 0 && queue.size() >= max_size && !m_abort_request.load()) {
		//cerr << "FrameQueue::push: Queue is full. Holding frame and wait." << endl;
		cond_producer.wait(lock);
	}

	// 等待结束后，再次检查是否是由于 abort 被唤醒
	if (m_abort_request.load()) {
		// 如果是 abort 导致的唤醒，则不再推入数据，并释放已克隆的帧，防止内存泄漏
		av_frame_free(&frame_clone);
		// cerr << "FrameQueue::push: Abort requested while waiting to push. Discarding frame." << endl;
		return false;
	}

	// 如果在等待期间被通知EOF，则不再推入
	if (eof_signaled.load()) {
		av_frame_free(&frame_clone);
		return false;
	}

	queue.push(frame_clone);

	lock.unlock();
	cond_consumer.notify_one();

	return true;
}

bool FrameQueue::pop(AVFrame* frame, int timeout_ms) {
	if (!frame) {
		cerr << "FrameQueue::pop: Output frame parameter is null." << endl;
		return false;
	}

	std::unique_lock<std::mutex> lock(mutex);

	// 当队列为空、且未收到EOF信号、且未收到中断信号时 等待
	while (queue.empty() && !eof_signaled.load() && !m_abort_request.load()) {
		if (timeout_ms == 0) { // 非阻塞
			return false;
		}
		if (timeout_ms < 0) { // 无限等待
			cond_consumer.wait(lock);
		}
		else {
			// 判断线程的唤醒是否是因为超时
			if (cond_consumer.wait_for(lock, std::chrono::milliseconds(timeout_ms)) 
				== std::cv_status::timeout) {
				return false; // 等待超时
			}
		}
	}

	// 检查唤醒的原因
	// 是中断信号生效
	if (m_abort_request.load()) {
		return false;
	}
	// 是队列为空且已收到EOF信号，认为流结束
	if (queue.empty() && eof_signaled.load()) {
		return false;
	}

	// 如果队列仍然是空的 (可能发生了虚假唤醒且没有abort/eof)，也返回false
	if (queue.empty()) {
		return false;
	}

	// 从队列中取出一个帧
	AVFrame* src_frame = queue.front();
	queue.pop();
	lock.unlock();

	// unref旧的，ref新的
	av_frame_unref(frame);
	int ret = av_frame_ref(frame, src_frame);
	if (ret < 0) {
		cerr << "FrameQueue::pop: av_frame_ref failed to copy to output frame. Error: " << ret << endl;
		// 即使引用失败，src_frame 也必须被释放
		av_frame_free(&src_frame);
		// 数据未能成功传递给调用者
		return false;
	}
	// 释放 src_frame 本身（在 push 时分配，其数据现在由外部 frame 引用）
	av_frame_free(&src_frame);
	// 通知一个可能在等待的生产者
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

	// 重置状态标志
	eof_signaled = false;
	m_abort_request = false;

	// 唤醒所有可能在等待的线程
	cond_consumer.notify_all(); // 唤醒等待 pop 的消费者
	cond_producer.notify_all(); // 唤醒等待 push 的生产者
}

void FrameQueue::signal_eof() {
	std::unique_lock<std::mutex> lock(mutex);
	eof_signaled = true;
	lock.unlock();
	// 唤醒所有等待的消费者和生产者，让他们能够检查eof_signaled标志并退出
	cond_consumer.notify_all();// 通知所有等待的消费者线程EOF状态已改变
	cond_producer.notify_all();// 通知所有等待的生产者线程
}

void FrameQueue::abort() {
	std::unique_lock<std::mutex> lock(mutex);
	m_abort_request.store(true);
	lock.unlock();
	cond_consumer.notify_all(); // 唤醒所有等待的消费者，让他们检查abort标志
	cond_producer.notify_all();// 唤醒所有等待的生产者线程
}

bool FrameQueue::is_eof() const {
	std::lock_guard<std::mutex> lock(mutex);
	return eof_signaled && queue.empty();
}
