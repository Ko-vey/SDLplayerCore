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

#include "../include/PacketQueue.h"
#include <iostream>

using namespace std;

PacketQueue::PacketQueue(size_t max_packet_count, int64_t max_duration_in_ts, bool block_on_full)
	: max_size(max_packet_count), 
	max_duration_ts(max_duration_in_ts),
	m_block_on_full(block_on_full)
{
}

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

	// 检查队列是否已满
	bool is_full = (max_size > 0 && queue.size() >= max_size);

	if (is_full) {
		if (m_block_on_full) {
			// 【本地文件模式】阻塞等待，直到不满
			while (is_full && !m_abort_request.load()) {
				cond_producer.wait(lock);
				// 唤醒后重新检查状态
				is_full = (max_size > 0 && queue.size() >= max_size);
			}
			if (m_abort_request.load()) {
				av_packet_free(&pkt_clone);
				return false;
			}
		}
		else {
			// 【直播模式】丢弃旧包
			while ((max_size > 0 && queue.size() >= max_size))
			{
				if (queue.empty()) break;
				// 丢包
				PacketData old_data = queue.front();
				queue.pop();
				// 更新统计数据
				m_total_bytes -= old_data.pkt->size;
				av_packet_free(&old_data.pkt);
				// cout << "Drop packet for live stream latency control" << endl;
			}
		}
	}

	// 将新包入队并更新统计
	queue.push({ pkt_clone, serial });
	m_total_bytes += pkt_clone->size;

	lock.unlock();
	cond_consumer.notify_one();
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

	// 唤醒可能在等待的生产者
	if (m_block_on_full) {
		cond_producer.notify_one();
	}

	return true;
}

size_t PacketQueue::size() const {
	std::lock_guard<std::mutex> lock(mutex);
	return queue.size();
}

int64_t PacketQueue::getTotalDuration() const {
	std::lock_guard<std::mutex> lock(mutex);

	if (queue.empty()) {
		return 0;
	}

	// 获取队首和队尾
	const PacketData& first = queue.front();
	const PacketData& last = queue.back();

	// 检查 PTS 有效性 (AV_NOPTS_VALUE)
	if (first.pkt->pts == AV_NOPTS_VALUE || last.pkt->pts == AV_NOPTS_VALUE) {
		// 如果无法获取有效 PTS，返回 0
		return 0;
	}

	int64_t duration = last.pkt->pts - first.pkt->pts;

	// 处理 PTS 回绕 (Wrap-around) 或 乱序导致的负值
	if (duration < 0) {
		return 0;
	}

	return duration;
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
	// 重置统计数据
	m_total_bytes = 0;

	// 重置状态标志，使其可以重新接收数据
	eof_signaled = false;
	m_abort_request = false;

	// 唤醒消费者和生产者线程
	cond_consumer.notify_all();
	cond_producer.notify_all();
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

	// 唤醒消费者和生产者线程
	cond_consumer.notify_all();
	cond_producer.notify_all();
}

bool PacketQueue::is_eof() const {
	std::lock_guard<std::mutex> lock(mutex);
	return eof_signaled.load() && queue.empty();
}
