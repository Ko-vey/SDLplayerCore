#pragma once

#include <string>

//FFmpeg���͵�ǰ�������������ڽӿ���ֱ�Ӱ���ͷ�ļ�
struct AVCodecParameters;//������������ṹ��
struct AVPacket;//���ݰ��ṹ��
struct AVFrame;//����֡�ṹ��
enum AVPixelFormat;//���ظ�ʽö�٣�����getPixelFormat()
struct AVRational;//FFmpeg�����ڱ�ʾ�������Ľṹ����ʱ���׼��֡�ʣ�

class IVideoDecoder {
public:
	//������������ȷ�����������ȷ����
	virtual ~IVideoDecoder() = default;

	/**
	* @brief ʹ�ø����ı��������������ʼ����Ƶ������
	* @param codecParams ָ��ӽ⸴������ȡ����Ƶ���� AVCodecParameters ��ָ��
	* @return ����ʼ���ɹ�����true�����򷵻�false��
	*/
	virtual bool init(AVCodecParameters* codecParams) = 0;

	/**
	* @brief ��������Ƶ������Ϊһ����Ƶ֡��
	* �����߸������packet��frame���������ڡ�
	* @param packet �����������ѹ����Ƶ���ݵ� AVPacket��
	* ����������ʱ��ȡ��������ʣ���֡��draining the decoder����
	* ͨ���Ƿ���һ���յ�packet��packet->data==nullptr && packet->size==0������������
	* @param frame ָ�� AVFrame ָ���ָ�룬��ָ�뽫����������Ƶ������䣬
	* ������������ڲ����з���/��ȡһ��֡��
	* @return �ɹ�ʱ����0��֡�ѽ��룩������Ҫ���������򷵻� AVERROR(EAGAIN)��
	* ��������ĩβ�򷵻�AVERROR_EOF��ʧ��ʱ���ظ��Ĵ�����롣
	*/
	virtual int decode(AVPacket* packet, AVFrame** frame) = 0;

	/**
	* @brief �رս��������ͷ����������Դ��
	* ���ô˷����󣬽�����ʵ���������á�
	*/
	virtual void close() = 0;

	/**
	* @brief ��ȡ��������Ƶ֡�Ŀ��
	* @return ��ȣ���λ�����أ���
	*/
	virtual int getWidth() const = 0;

	/**
	* @brief ��ȡ��������Ƶ֡�ĸ߶�
	* @return �߶ȣ���λ�����أ���
	*/
	virtual int getHeight() const = 0;

	/**
	* @brief ��ȡ��������Ƶ֡�����ظ�ʽ
	* @return AVPixelFormat ö��ֵ��
	*/
	virtual AVPixelFormat getPixelFormat() const = 0;

	/**
	* @brief ��ȡ��Ƶ����ʱ���׼��time base����
	* ʱ���׼������ʱ����Ļ�����λ��
	* @return ��ʾʱ���׼�� AVRational �ṹ�塣
	*/
	virtual struct AVRational getTimeBase() const = 0;

	/**
	* @brief ��ȡ��Ƶ����ƽ��֡�ʡ�
	* @return ��ʾƽ��֡�ʵ� AVRational �ṹ�塣
	*/
	virtual struct AVRational getFrameRate() const = 0;
};
