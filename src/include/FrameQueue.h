#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>//����ʱ�� std::chrono::milliseconds

//AVPacket �� AVFrame
extern "C" {
#include <libavcodec/avcodec.h>
}

using namespace std;

class FrameQueue {
private:
	std::queue<AVFrame*> queue;
	mutable std::mutex mutex;//�ɱ����ʡ��޸ĵ��������ͣ�mutable ������const������lock
	std::condition_variable cond_consumer;	//������Ϊ��ʱ�������ߵȴ�
	std::condition_variable cond_producer;	//����������ʱ�������ߵȴ�
	bool eof_signaled = false;
	// size_t - �޷����������ͣ�����������ƽ̨����
	size_t max_size = 0;		// 0��ʾ�����ƣ�>0��ʾ�����������

public:
	FrameQueue(size_t max_queue_size = 0);	//���캯������ָ�������д�С
	~FrameQueue();							//����������������Դ

	/**
	* @brief �������֡������β����
	* �ڲ���Ϊframe����һ���µ����ø������д洢��
	* ������Ϊ����Ϊ��ʱ��������������
	* @return true - �ɹ���false - ʧ�ܣ����������������frameΪnull��
	*/
	bool push(AVFrame* frame);

	/**
	* @brief �Ӷ���ͷ����ȡ֡�����ݻ����õ��������ṩ��frame��
	* @param frame: �������ṩ��AVFrameָ�룬���ڽ������ݡ�����ǰӦȷ�����ѷ���(av_frame_alloc)
	* ����֮ǰ���õ�����unref������������unref����ref
	* @param timeout_ms���ȴ���ʱʱ�䣨���룩
	* <0:���޵ȴ�
	* 0��������
	* >0���ȴ�ָ��ʱ��
	* @return �ɹ���ȡframe����true��ʧ���򷵻�false����ʱ������Ϊ����EOF�������Ϊ�յķ��������ã�
	*/
	bool pop(AVFrame* frame, int timeout_ms = -1);

	//��ȡ���е�ǰԪ������
	size_t size() const;

	//��ն����е�����֡�����ͷ�����Դ
	void clear();

	//֪ͨ����������������EOF�����ỽ�����еȴ�pop������
	void signal_eof();

	//����Ƿ���֪ͨEOF�Ҷ����ѿ�
	bool is_eof() const;

	//��ֹ�������캯���͸�ֵ������
	FrameQueue(const FrameQueue&) = delete;
	FrameQueue& operator=(const FrameQueue&) = delete;
};