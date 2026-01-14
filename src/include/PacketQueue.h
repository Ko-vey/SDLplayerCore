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
#include <chrono>	// std::chrono::milliseconds
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h> // AVPacket & AVFrame
}

using namespace std;

class PacketQueue {
private:
	// 内部数据存储结构
	struct PacketData {
		AVPacket* pkt;
		int serial;
	};

	std::queue<PacketData> queue;
	mutable std::mutex mutex;				// mutable 允许在 const 方法中 lock
	std::condition_variable cond_consumer;	// 当队列为空时，消费者等待
	std::condition_variable cond_producer;  // 当队列为满时，生产者阻塞
	bool m_block_on_full = false;           // 生产者阻塞标志：true=阻塞，false=丢包

	std::atomic<bool> eof_signaled{ false };	// 流结束标志
	std::atomic<bool> m_abort_request{ false }; // 强制中断标志
	
	// 统计相关
	size_t m_total_bytes = 0;        // 队列中所有packet的总字节数

	// 时长限制
	size_t max_size = 0;             // 队列最大包数量限制 (0=无限制)
	int64_t max_duration_ts = 0;     // 队列最大总时长限制 (0=无限制)

public:
	/**
	 * @brief 构造函数
	 * @param max_packet_count 队列允许的最大包数量，0表示无限制
	 * @param max_duration_in_ts 队列允许的最大缓冲时长（以AVStream->time_base为单位），0表示无限制
	 * @param block_on_full 队列的阻塞策略，true-满则等，false-满则丢
	 */
	PacketQueue(size_t max_packet_count = 0, int64_t max_duration_in_ts = 0, bool block_on_full = false);

	~PacketQueue();

	/**
	 * @brief 添加数据包和对应的序列号
	 * @param serial 当前的播放序列号
	 */
	bool push(AVPacket* packet, int serial);

	/**
	 * @brief 获取数据包和对应的序列号
	 * @param serial 输出参数，返回该包的序列号
	 */
	bool pop(AVPacket* packet, int& serial, int timeout_ms = -1);

	size_t size() const;

	/**
	* @brief 获取队列当前缓冲的总时长 (以时间基为单位)
	*/
	int64_t getTotalDuration() const;

	/**
	* @brief 获取队列当前缓冲的总字节数
	*/
	size_t getTotalBytes() const;

	/**
	* @brief 清空队列中的所有数据包，并重置所有统计信息
	*/
	void clear();

	/**
	* @brief 通知队列数据流结束（EOF），会唤醒等待pop的消费者
	*/
	void signal_eof();

	/**
	* @brief 强制中断所有等待操作(push/pop)，用于程序退出
	*/
	void abort();

	/**
	* @brief 检查是否已通知EOF且队列已空
	*/
	bool is_eof() const;

	PacketQueue(const PacketQueue&) = delete;
	PacketQueue& operator=(const PacketQueue&) = delete;
};
