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
	// 为输入frame的数据创建一个新的引用，并由frame_clone持有
	if (av_frame_ref(frame_clone, frame) < 0) {
		cerr << "FrameQueue::push: av_frame_ref failed." << endl;
		av_frame_free(&frame_clone); // 释放刚分配的frame_clone结构
		return false;
	}

	std::unique_lock<std::mutex> lock(mutex);
	
	// 检查是否需要中止操作
	if (m_abort_request.load() || eof_signaled.load()) {
		av_frame_free(&frame_clone);
		return false;
	}

	// “满则丢”的滑动窗口机制
	if (max_size > 0 && queue.size() >= max_size) {
		// 丢弃队列头部的帧（最老的帧）来为新帧腾出空间
		AVFrame* oldest_frame = queue.front();
		queue.pop();
		av_frame_free(&oldest_frame);
		// 可选调试信息
		//cerr << "FrameQueue: Dropped a frame to make space." << endl;
	}

	queue.push(frame_clone);

	lock.unlock(); // 在通知前，尽早释放锁
	// 通知一个在等待的消费者，队列中有新数据
	cond_consumer.notify_one();

	return true;
}

bool FrameQueue::pop(AVFrame* frame, int timeout_ms) {
	if (!frame) {
		cerr << "FrameQueue::pop: Output frame parameter is null." << endl;
		return false;
	}

	std::unique_lock<std::mutex> lock(mutex);

	// 当队列为空且未收到EOF信号时等待
	while (queue.empty() && !eof_signaled) {
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

	// 如果队列为空且已收到EOF信号，则认为流结束
	if (queue.empty() && eof_signaled) {
		return false;
	}

	// 从队列中取出一个帧
	AVFrame* src_frame = queue.front();
	queue.pop();
	lock.unlock(); // 在执行FFmpeg操作前可以释放锁

	// unref旧的，ref新的
	av_frame_unref(frame);
	int ret = av_frame_ref(frame, src_frame);
	if (ret < 0) {
		cerr << "FrameQueue::pop: av_frame_ref failed to copy to output frame. Error: " << ret << endl;
		// 即使引用失败，src_frame 也必须被正确处理
		// 此时用户提供的 frame 可能处于不确定状态，但仍需释放 src_frame
		av_frame_free(&src_frame);
		// 返回 false，因为数据未能成功传递给调用者
		return false;
	}

	// 释放 src_frame本身（它在 push 时分配，其数据现在由外部 frame 引用）
	av_frame_free(&src_frame); // 释放开发者自己管理的副本的容器

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
	// eof_signaled 状态通常在clear时不改变，
	// 因为clear可能是中间操作，EOF代表流的结束。
	// 如果需要完全重置，可以添加一个reset()方法。
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
