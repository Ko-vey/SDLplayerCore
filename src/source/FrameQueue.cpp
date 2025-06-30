#include "../include/FrameQueue.h"
#include <iostream>//�������

FrameQueue::FrameQueue(size_t max_queue_size):max_size(max_queue_size) {}

FrameQueue::~FrameQueue() { clear(); }

bool FrameQueue::push(AVFrame* frame) {
	if (!frame) {
		cerr << "FrameQueue::push: Input frame is null." << endl;
		return false;
	}

	AVFrame* frame_clone = av_frame_alloc();
	if (!frame_clone) {
		cerr << "FrameQueue::push: av_frame_alloc failed." << endl;
		return false;//����ʧ��
	}

	//Ϊ����frame�����ݴ���һ���µ����ã�����frame_clone����
	int ret = av_frame_ref(frame_clone, frame);
	if (ret < 0) {
		cerr << "FrameQueue::push: av_frame_ref failed with error " << ret << endl;
		av_frame_free(&frame_clone);//�ͷŸշ����frame_clone�ṹ
		return false;
	}

	std::unique_lock<std::mutex> lock(mutex);
	
	// ���������������������Ҷ�����������ȴ�
	// ����ʹ��whileѭ������ֹ����ٻ��ѡ�(spurious wakeups)
	while (max_size > 0 && queue.size() >= max_size && !eof_signaled) {
		//cerr << "FrameQueue::push: Queue is full. Holding frame and wait." << endl;
		cond_producer.wait(lock);
	}

	// ����ڵȴ��ڼ䱻֪ͨEOF����������
	if (eof_signaled) {
		return false;
	}

	queue.push(frame_clone);

	lock.unlock();//��֪֮ͨǰ�������ͷ���
	// ֪ͨһ�����ڵȴ��������ߣ�����������������
	cond_consumer.notify_one();

	return true;
}

bool FrameQueue::pop(AVFrame* frame, int timeout_ms) {
	if (!frame) {//Ŀ��frameָ�벻��Ϊ��
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
	lock.unlock();//��ִ��FFmpeg����ǰ�����ͷ���

	// unref�ɵģ�ref�µ�
	av_frame_unref(frame);
	int ret = av_frame_ref(frame, src_frame);
	if (ret < 0) {
		cerr << "FrameQueue::pop: av_frame_ref failed to copy to output frame. Error: " << ret << endl;
		//��ʹ����ʧ�ܣ�src_frameҲ���뱻��ȷ����
		//��ʱ�û��ṩ��frame���ܴ��ڲ�ȷ��״̬���������ͷ�src_frame
		av_frame_free(&src_frame);
		//����false����Ϊ����δ�ܳɹ����ݸ�������
		return false;
	}

	//�ͷ�src_frame��������pushʱ���䣬�������������ⲿframe���ã�
	av_frame_free(&src_frame);//�ͷ������Լ�������Ǹ�����������

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
		av_frame_free(&frm);//�ͷ�AVFrame�ṹ����
	}
	// eof_signaled ״̬ͨ����clearʱ���ı䣬
	// ��Ϊclear�������м������EOF�������Ľ�����
	//�����Ҫ��ȫ���ã��������һ��reset()������
}

void FrameQueue::signal_eof() {
	std::unique_lock<std::mutex> lock(mutex);
	eof_signaled = true;
	lock.unlock();
	// �������еȴ��������ߺ������ߣ��������ܹ����eof_signaled��־���˳�
	cond_consumer.notify_all();//֪ͨ���еȴ����������߳�EOF״̬�Ѹı�
	cond_producer.notify_all();//֪ͨ���еȴ����������߳�
}

bool FrameQueue::is_eof() const {
	std::lock_guard<std::mutex> lock(mutex);
	return eof_signaled && queue.empty();
}
