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
	std::queue<AVPacket*> queue;
	mutable std::mutex mutex;				// mutable 允许在 const 方法中 lock
	std::condition_variable cond_consumer;	// 当队列为空时，消费者等待

	std::atomic<bool> eof_signaled{ false };	// 流结束标志
	std::atomic<bool> m_abort_request{ false }; // 强制中断标志
	
	// 统计相关
	int64_t m_total_duration_ts = 0; // 队列中所有packet的总时长 (以AVStream->time_base为单位)
	size_t m_total_bytes = 0;        // 队列中所有packet的总字节数

	// 时长限制
	size_t max_size = 0;             // 队列最大包数量限制 (0=无限制)
	int64_t max_duration_ts = 0;     // 队列最大总时长限制 (0=无限制)

public:
	/**
	 * @brief 构造函数
	 * @param max_packet_count 队列允许的最大包数量，0表示无限制
	 * @param max_duration_in_ts 队列允许的最大缓冲时长（以AVStream->time_base为单位），0表示无限制
	 */
	PacketQueue(size_t max_packet_count = 0, int64_t max_duration_in_ts = 0);
	~PacketQueue();

	/**
	* @brief 添加数据包到队列尾部。
	* 如果队列已满（包数量或总时长超出限制），将从队列头部丢弃最老的数据包（滑动窗口），直到有空间为止。
	* 此函数不再阻塞。
	* @return true - 成功，false - 失败（例如队列已中止或packet为null）
	*/
	bool push(AVPacket* packet);

	/**
	* @brief 从队列头部获取数据包。
	* @param packet 调用者提供的AVPacket指针，用于接收数据。
	* @param timeout_ms 等待超时时间（毫秒）。<0:无限等待, 0:非阻塞, >0:等待指定时间。
	* @return 成功获取返回true，失败返回false（超时、队列为空且EOF、或被abort）。
	*/
	bool pop(AVPacket* packet, int timeout_ms = -1);

	/**
	* @brief 获取队列当前元素数量
	*/
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
