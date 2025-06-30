#pragma once

#include "IVideoDecoder.h"

//ǰ������ FFmpeg���ͣ��Ա�����ͷ�ļ��а����Ӵ��FFmpegͷ�ļ�
//IVideoDecoder.h�Ѿ�ǰ�������� AVCodecParameters��AVPacket��AVFrame��AVPixelFormat��AVRational
struct AVCodecContext;

class FFmpegVideoDecoder : public IVideoDecoder {
public:
	FFmpegVideoDecoder();
	~FFmpegVideoDecoder() override;

	//��ֹ�������캯���͸�ֵ������
	FFmpegVideoDecoder(const FFmpegVideoDecoder&) = delete;
	FFmpegVideoDecoder& operator=(const FFmpegVideoDecoder&) = delete;

	/**
	* @brief ʹ�ø����ı������������ʱ�ӹ����� ��ʼ����Ƶ��������
	* @param codecParams ָ��ӽ⸴������ȡ����Ƶ���� AVCodecParameters ��ָ�롣
	* @return ����ʼ���ɹ����� true�����򷵻� false��
	*/
	bool init(AVCodecParameters* codecParams) override;

	/**
	* @brief ��������Ƶ������Ϊһ��������Ƶ֡��
	* �����߸������ packet ���������ڡ�
	* �������ɹ������������һ���µ� AVFrame ��ͨ�� frame �������ء��������ڴ������ frame ��
	* ������� av_frame_free() ���ͷ�����
	* @param packet �����������ѹ����Ƶ���ݵ� AVPacket��
	* ��Ϊ nullptr�����ʾ��ϴ����������ȡ�ڲ������֡����
	* @param frame ָ�� AVFrame ָ���ָ�롣�������ɹ���*frame ��ָ���·���Ľ���֡��
	* �������ʧ�ܻ���Ҫ�������룬*frame������Ϊ nullptr��
	* @return �ɹ�ʱ���� 0��֡�ѽ��벢���� *frame����
	* ����Ҫ���������򷵻� AVERROR(EAGAIN)����ʱ *frame Ϊnullptr
	* ��������ĩβ�򷵻� AVERROR_EOF����ʱ *frame Ϊ nullptr����
	* ������ֵ��ʾ�������
	*/
	int decode(AVPacket* packet, AVFrame** frame) override;

	/**
	* @brief �رս��������ͷ�������� FFmpeg ��Դ��
	* ���ô˷����󣬽�����ʵ���������ã������ٴε��� init()��
	*/
	void close() override;

	/**
	* @brief ��ȡ��������Ƶ֡�Ŀ�ȡ�
	* @return ��ȣ���λ�����أ������������δ��ʼ���򷵻� 0��
	*/
	int getWidth() const override;
	
	/**
	* @brief ��ȡ��������Ƶ֡�ĸ߶ȡ�
	* @return �߶ȣ���λ�����أ������������δ��ʼ���򷵻� 0��
	*/
	int getHeight() const override;

	/**
	* @brief ��ȡ��������Ƶ֡�����ظ�ʽ��
	* @return AVPixelFormat ö��ֵ�����������δ��ʼ���򷵻� AV_PIX_FMT_NONE��
	*/
	AVPixelFormat getPixelFormat() const override;

	/**
	* @brief ��ȡ��Ƶ����ʱ���׼��
	* @return ��ʾʱ���׼�� AVRational �ṹ�壬���������δ��ʼ���򷵻�{0,1}��
	*/
	AVRational getTimeBase() const override;
	
	/**
	* @brief ��ȡ��Ƶ����ƽ��֡�ʡ�
	* @return ��ʾƽ��֡�ʵ� AVRational �ṹ�壬���������δ��ʼ���򷵻�{0,1}��
	*/
	AVRational getFrameRate() const override;

private:
	AVCodecContext* m_codecContext;	// FFmpeg ������������
};
