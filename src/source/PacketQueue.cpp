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
#include <iostream>//调试输出

PacketQueue::PacketQueue(size_t max_queue_size):max_size(max_queue_size) {}

PacketQueue::~PacketQueue() { clear(); }

bool PacketQueue::push(AVPacket* packet) {
	if (!packet) {
		cerr << "PacketQueue::push: Input packet is null." << endl;
		return false;
	}

	AVPacket* pkt_clone = av_packet_alloc();
	if (!pkt_clone) {
		cerr << "PacketQueue::push: av_packet_alloc failed." << endl;
		return false;//分配失败
	}

	//为输入packet的数据创建一个新的引用，并由pkt_clone持有
	int ret = av_packet_ref(pkt_clone, packet);
	if (ret < 0) {
		cerr << "PacketQueue::push: av_packet_ref failed with error " << ret << endl;
		av_packet_free(&pkt_clone);//释放刚分配的pkt_clone结构
		return false;
	}

	std::unique_lock<std::mutex> lock(mutex);

	// 如果设置了最大容量，并且队列已满，则等待
	// 必须使用while循环来防止“虚假唤醒”(spurious wakeups)
	while (max_size > 0 && queue.size() >= max_size && !eof_signaled) {
		//cerr << "PacketQueue::push: Queue is full. Holding packet and wait." << endl;
		cond_producer.wait(lock);
	}

	// 如果在等待期间被通知EOF，则不再推入
	if (eof_signaled) {
		return false;
	}

	queue.push(pkt_clone);

	lock.unlock();//在通知之前，尽早释放锁
	// 通知一个正在等待的消费者，队列中有新数据了
	cond_consumer.notify_one();

	return true;
}

bool PacketQueue::pop(AVPacket* packet, int timeout_ms) {
	if (!packet) {//目标packet指针不能为空
		cerr << "PacketQueue::pop: Output packet parameter is null." << endl;
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

	// 从队列中取出一个包
	AVPacket* src_pkt = queue.front();
	queue.pop();
	lock.unlock();//在执行FFmpeg操作前可以释放锁

	// unref旧的，ref新的
	av_packet_unref(packet);
	int ret = av_packet_ref(packet, src_pkt);
	if (ret < 0) {
		cerr << "PacketQueue::pop: av_packet_ref failed to copy to output packet. Error: " << ret << endl;
		//即使引用失败，src_pkt也必须被正确处理
		//此时用户提供的packet可能处于不确定状态，但仍需释放src_pkt
		av_packet_free(&src_pkt);
		//返回false，因为数据未能成功传递给调用者
		return false;
	}

	//释放src_pkt本身（它在push时分配，其数据现在由外部packet引用）
	av_packet_free(&src_pkt);//释放我们自己管理的那个副本的容器

	// 通知一个可能在等待的生产者，队列中有空间了
	cond_producer.notify_one();

	return true;
}

size_t PacketQueue::size() const {
	std::lock_guard<std::mutex> lock(mutex);
	return queue.size();
}

void PacketQueue::clear() {
	std::unique_lock<std::mutex> lock(mutex);
	while (!queue.empty()) {
		AVPacket* pkt = queue.front();
		queue.pop();
		av_packet_free(&pkt);//释放AVPacket结构本身
	}
	// eof_signaled 状态通常在clear时不改变，
	// 因为clear可能是中间操作，EOF代表流的结束。
	//如果需要完全重置，可以添加一个reset()方法。
}

void PacketQueue::signal_eof() {
	std::unique_lock<std::mutex> lock(mutex);
	eof_signaled = true;
	lock.unlock();
	// 唤醒所有等待的消费者和生产者，让他们能够检查eof_signaled标志并退出
	cond_consumer.notify_all();//通知所有等待的消费者线程EOF状态已改变
	cond_producer.notify_all();//通知所有等待的生产者线程
}

bool PacketQueue::is_eof() const {
	std::lock_guard<std::mutex> lock(mutex);
	return eof_signaled && queue.empty();
}

