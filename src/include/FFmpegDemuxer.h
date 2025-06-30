//具体 使用FFmpeg库 来 实现解封装器Demuxer类 的 头文件
//FFmpegDemuxer继承于IDemuxer接口，负责处理所有FFmpeg相关的复杂细节

#pragma once

#include "../include/IDemuxer.h"
#include <string>

using namespace std;

//包含用于实现的实际FFmpeg头文件
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>	// AVCodecParameters, AVMediaType
#include <libavutil/dict.h>		//若还需要加入元数据功能
#include <libavutil/time.h>		//AV_TIME_BASE定义在这里
}

class FFmpegDemuxer : public IDemuxer {//继承接口
private:
	AVFormatContext* pFormatCtx = nullptr;
	string m_url;
	int m_videoStreamIndex = -1;
	int m_audioStreamIndex = -1;
	//其它需要缓存的信息

public:
	FFmpegDemuxer();
	//重写 虚析构函数
	virtual ~FFmpegDemuxer() override;

	//重写并实现所有的继承于IDemuxer的纯虚函数
	bool open(const char* url) override;
	void close() override;
	int readPacket(AVPacket* packet) override;
	AVFormatContext* getFormatContext() const override;
	int findStream(AVMediaType type) const override;
	AVCodecParameters* getCodecParameters(int streamIndex) const override;
	AVRational getTimeBase(int streamIndex) const override;
	double getDuration() const override;

	//禁止复制构造函数和赋值操作符重载
	FFmpegDemuxer(const FFmpegDemuxer&) = delete;
	FFmpegDemuxer& operator=(const FFmpegDemuxer&) = delete;

private:
	//辅助函数，用于在打开文件后查找流
	void findStreamsInternal();
};
