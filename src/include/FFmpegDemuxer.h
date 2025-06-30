//���� ʹ��FFmpeg�� �� ʵ�ֽ��װ��Demuxer�� �� ͷ�ļ�
//FFmpegDemuxer�̳���IDemuxer�ӿڣ�����������FFmpeg��صĸ���ϸ��

#pragma once

#include "../include/IDemuxer.h"
#include <string>

using namespace std;

//��������ʵ�ֵ�ʵ��FFmpegͷ�ļ�
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>	// AVCodecParameters, AVMediaType
#include <libavutil/dict.h>		//������Ҫ����Ԫ���ݹ���
#include <libavutil/time.h>		//AV_TIME_BASE����������
}

class FFmpegDemuxer : public IDemuxer {//�̳нӿ�
private:
	AVFormatContext* pFormatCtx = nullptr;
	string m_url;
	int m_videoStreamIndex = -1;
	int m_audioStreamIndex = -1;
	//������Ҫ�������Ϣ

public:
	FFmpegDemuxer();
	//��д ����������
	virtual ~FFmpegDemuxer() override;

	//��д��ʵ�����еļ̳���IDemuxer�Ĵ��麯��
	bool open(const char* url) override;
	void close() override;
	int readPacket(AVPacket* packet) override;
	AVFormatContext* getFormatContext() const override;
	int findStream(AVMediaType type) const override;
	AVCodecParameters* getCodecParameters(int streamIndex) const override;
	AVRational getTimeBase(int streamIndex) const override;
	double getDuration() const override;

	//��ֹ���ƹ��캯���͸�ֵ����������
	FFmpegDemuxer(const FFmpegDemuxer&) = delete;
	FFmpegDemuxer& operator=(const FFmpegDemuxer&) = delete;

private:
	//���������������ڴ��ļ��������
	void findStreamsInternal();
};
