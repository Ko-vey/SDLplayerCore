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
#include <iostream>//�������

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
		return false;//����ʧ��
	}

	//Ϊ����packet�����ݴ���һ���µ����ã�����pkt_clone����
	int ret = av_packet_ref(pkt_clone, packet);
	if (ret < 0) {
		cerr << "PacketQueue::push: av_packet_ref failed with error " << ret << endl;
		av_packet_free(&pkt_clone);//�ͷŸշ����pkt_clone�ṹ
		return false;
	}

	std::unique_lock<std::mutex> lock(mutex);

	// ���������������������Ҷ�����������ȴ�
	// ����ʹ��whileѭ������ֹ����ٻ��ѡ�(spurious wakeups)
	while (max_size > 0 && queue.size() >= max_size && !eof_signaled) {
		//cerr << "PacketQueue::push: Queue is full. Holding packet and wait." << endl;
		cond_producer.wait(lock);
	}

	// ����ڵȴ��ڼ䱻֪ͨEOF����������
	if (eof_signaled) {
		return false;
	}

	queue.push(pkt_clone);

	lock.unlock();//��֪֮ͨǰ�������ͷ���
	// ֪ͨһ�����ڵȴ��������ߣ�����������������
	cond_consumer.notify_one();

	return true;
}

bool PacketQueue::pop(AVPacket* packet, int timeout_ms) {
	if (!packet) {//Ŀ��packetָ�벻��Ϊ��
		cerr << "PacketQueue::pop: Output packet parameter is null." << endl;
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

	// �Ӷ�����ȡ��һ����
	AVPacket* src_pkt = queue.front();
	queue.pop();
	lock.unlock();//��ִ��FFmpeg����ǰ�����ͷ���

	// unref�ɵģ�ref�µ�
	av_packet_unref(packet);
	int ret = av_packet_ref(packet, src_pkt);
	if (ret < 0) {
		cerr << "PacketQueue::pop: av_packet_ref failed to copy to output packet. Error: " << ret << endl;
		//��ʹ����ʧ�ܣ�src_pktҲ���뱻��ȷ����
		//��ʱ�û��ṩ��packet���ܴ��ڲ�ȷ��״̬���������ͷ�src_pkt
		av_packet_free(&src_pkt);
		//����false����Ϊ����δ�ܳɹ����ݸ�������
		return false;
	}

	//�ͷ�src_pkt��������pushʱ���䣬�������������ⲿpacket���ã�
	av_packet_free(&src_pkt);//�ͷ������Լ�������Ǹ�����������

	// ֪ͨһ�������ڵȴ��������ߣ��������пռ���
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
		av_packet_free(&pkt);//�ͷ�AVPacket�ṹ����
	}
	// eof_signaled ״̬ͨ����clearʱ���ı䣬
	// ��Ϊclear�������м������EOF�������Ľ�����
	//�����Ҫ��ȫ���ã��������һ��reset()������
}

void PacketQueue::signal_eof() {
	std::unique_lock<std::mutex> lock(mutex);
	eof_signaled = true;
	lock.unlock();
	// �������еȴ��������ߺ������ߣ��������ܹ����eof_signaled��־���˳�
	cond_consumer.notify_all();//֪ͨ���еȴ����������߳�EOF״̬�Ѹı�
	cond_producer.notify_all();//֪ͨ���еȴ����������߳�
}

bool PacketQueue::is_eof() const {
	std::lock_guard<std::mutex> lock(mutex);
	return eof_signaled && queue.empty();
}

