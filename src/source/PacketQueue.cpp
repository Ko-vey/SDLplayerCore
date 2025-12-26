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

#include "../include/PacketQueue.h"
#include <iostream>

PacketQueue::PacketQueue(size_t max_packet_count, int64_t max_duration_in_ts)
	: max_size(max_packet_count), max_duration_ts(max_duration_in_ts) {}

PacketQueue::~PacketQueue() { 
	clear(); 
}

bool PacketQueue::push(AVPacket* packet, int serial) {
	if (!packet) {
		cerr << "PacketQueue::push: Input packet is null." << endl;
		return false;
	}

	AVPacket* pkt_clone = av_packet_alloc();
	if (!pkt_clone) {
		cerr << "PacketQueue::push: av_packet_alloc failed." << endl;
		return false;
	}
	if (av_packet_ref(pkt_clone, packet) < 0) {
		cerr << "PacketQueue::push: av_packet_ref failed." << endl;
		av_packet_free(&pkt_clone);
		return false;
	}

	std::unique_lock<std::mutex> lock(mutex);

	// 检查是否需要中止操作
	if (m_abort_request.load() || eof_signaled.load()) {
		av_packet_free(&pkt_clone);
		return false;
	}

	// “满则丢”的滑动窗口机制
	// 如果队列超出限制，则从头部丢弃最老的数据包
	while ((max_size > 0 && queue.size() >= max_size) ||
		(max_duration_ts > 0 && (m_total_duration_ts + pkt_clone->duration) >= max_duration_ts))
	{
		if (queue.empty()) { // 一般不会发生，但用于安全检查
			break;
		}

		// 丢弃旧的包
		PacketData old_data = queue.front();
		queue.pop();

		// 更新统计数据
		m_total_duration_ts -= old_data.pkt->duration;
		m_total_bytes -= old_data.pkt->size;
		av_packet_free(&old_data.pkt);
	}

	// 将新包入队并更新统计
	queue.push({ pkt_clone, serial });

	m_total_duration_ts += pkt_clone->duration;
	m_total_bytes += pkt_clone->size;

	lock.unlock();
	cond_consumer.notify_one(); // 通知一个正在等待的消费者，有新数据了
	return true;
}

bool PacketQueue::pop(AVPacket* packet, int& serial, int timeout_ms) {
	if (!packet) {
		cerr << "PacketQueue::pop: Output packet parameter is null." << endl;
		return false;
	}

	std::unique_lock<std::mutex> lock(mutex);

	while (queue.empty() && !eof_signaled.load() && !m_abort_request.load()) {
		if (timeout_ms == 0) {
			return false;
		}
		if (timeout_ms < 0) {
			cond_consumer.wait(lock);
		}
		else {
			if (cond_consumer.wait_for(lock, std::chrono::milliseconds(timeout_ms)) == std::cv_status::timeout) {
				return false;
			}
		}
	}

	// 检查是否因中止或EOF退出等待
	if (m_abort_request.load()) {
		return false;
	}
	if (queue.empty() && eof_signaled.load()) {
		return false;
	}

	PacketData src_data = queue.front();
	queue.pop();

	// 同步更新统计数据
	m_total_duration_ts -= src_data.pkt->duration;
	m_total_bytes -= src_data.pkt->size;

	lock.unlock();

	// 复制 Packet 数据
	av_packet_unref(packet);
	if (av_packet_ref(packet, src_data.pkt) < 0) {
		cerr << "PacketQueue::pop: av_packet_ref failed." << endl;
		av_packet_free(&src_data.pkt);
		return false;
	}

	// 输出 serial
	serial = src_data.serial;

	av_packet_free(&src_data.pkt);
	return true;
}

size_t PacketQueue::size() const {
	std::lock_guard<std::mutex> lock(mutex);
	return queue.size();
}

int64_t PacketQueue::getTotalDuration() const {
	std::lock_guard<std::mutex> lock(mutex);
	return m_total_duration_ts;
}

size_t PacketQueue::getTotalBytes() const {
	std::lock_guard<std::mutex> lock(mutex);
	return m_total_bytes;
}

void PacketQueue::clear() {
	std::unique_lock<std::mutex> lock(mutex);
	while (!queue.empty()) {
		PacketData data = queue.front();
		queue.pop();
		av_packet_free(&data.pkt);
	}
	// 重置所有统计数据
	m_total_duration_ts = 0;
	m_total_bytes = 0;
	// 重置状态标志，使其可以重新接收数据
	eof_signaled = false;
	m_abort_request = false;
	// 唤醒可能因队列为空而等待的消费者线程
	cond_consumer.notify_all();
}

void PacketQueue::signal_eof() {
	std::unique_lock<std::mutex> lock(mutex);
	eof_signaled.store(true);
	lock.unlock();
	cond_consumer.notify_all();// 唤醒所有等待的消费者
}

void PacketQueue::abort() {
	std::unique_lock<std::mutex> lock(mutex);
	m_abort_request.store(true);
	lock.unlock();
	cond_consumer.notify_all(); // 唤醒所有等待的消费者，让他们检查abort标志
}

bool PacketQueue::is_eof() const {
	std::lock_guard<std::mutex> lock(mutex);
	return eof_signaled.load() && queue.empty();
}
