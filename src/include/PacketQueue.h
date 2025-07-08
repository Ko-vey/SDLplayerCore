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
#include <chrono>	// 跟踪时间 std::chrono::milliseconds

//AVPacket 和 AVFrame
extern "C" {
#include <libavcodec/avcodec.h>
}

using namespace std;

class PacketQueue {
private:
	std::queue<AVPacket*> queue;
	mutable std::mutex mutex;				// 可被访问、修改的数据类型；mutable 允许在const方法中lock
	std::condition_variable cond_consumer;	// 当队列为空时，消费者等待
	std::condition_variable cond_producer;	// 当队列已满时，生产者等待
	bool eof_signaled = false;
	// size_t - 无符号整数类型，具体类型由平台决定
	size_t max_size = 0;	// 0表示无限制，>0表示队列最大容量

public:
	PacketQueue(size_t max_queue_size = 0);	// 构造函数，可指定最大队列大小
	~PacketQueue();							// 析构函数，清理资源

	/**
	* @brief 添加数据包到队列尾部，
	* 内部会为packet创建一个新的引用副本进行存储。
	* 当容器为满或为空时，会陷入阻塞。
	* @return true - 成功，false - 失败（例如队列已满，或packet为null）
	*/
	bool push(AVPacket* packet);

	/**
	* @brief 从队列头部获取数据包，数据会引用到调用者提供的packet中
	* @param packet: 调用者提供的AVPacket指针，用于接收数据。
	* 调用前应确保其已分配(av_packet_alloc)，或将其之前引用的数据unref。本函数会先unref后再ref
	* @param timeout_ms：等待超时时间（毫秒）
	* <0:无限等待
	* 0：非阻塞
	* >0：等待指定时间
	* @return 成功获取packet返回true，失败则返回false（超时、队列为空且EOF、或队列为空的非阻塞调用）
	*/
	bool pop(AVPacket* packet, int timeout_ms = -1);

	/**
	* @brief 获取队列当前元素数量
	*/
	size_t size() const;

	/**
	* @brief 清空队列中的所有数据包，并释放其资源
	*/
	void clear();

	/**
	* @brief 通知队列数据流结束（EOF）；会唤醒所有等待pop的数据 和所有等待者
	*/
	void signal_eof();

	/**
	* @brief 检查是否已通知EOF且队列已空
	*/
	bool is_eof() const;

	// 禁止拷贝构造函数和赋值操作符
	PacketQueue(const PacketQueue&) = delete;
	PacketQueue& operator=(const PacketQueue&) = delete;
};
