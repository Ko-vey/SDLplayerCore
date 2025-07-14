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

#include "../include/FFmpegDemuxer.h"
#include <iostream> // 用于错误信息

using namespace std;

FFmpegDemuxer::FFmpegDemuxer() {
	// 构造函数：如果需要，在此初始化 类的成员
	// FFmpeg的全局初始化（如avformat_network_init()等）
	// 可能在其它地方完成，在每次应用运行时只执行一次
}

FFmpegDemuxer::~FFmpegDemuxer() {
	//析构函数确保调用了 close()
	close();
}

bool FFmpegDemuxer::open(const char* url) {
	//先确保关闭先前的上下文
	close();

	pFormatCtx = avformat_alloc_context();
	if (!pFormatCtx) {
		cerr << "FFmpegDemuxer Error: Counld not allocate format context." << endl;
		return false;
	}

	//打开输入文件
	if (avformat_open_input(&pFormatCtx, url, nullptr, nullptr) != 0) {
		cerr << "FFmpegDemuxer Error: Counldn't open input stream: " << url << endl;
		avformat_free_context(pFormatCtx);//需要释放上面分配的上下文
		pFormatCtx = nullptr;
		return false;
	}

	//检索流信息
	if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
		cerr << "FFmpegDemuxer Error: Couldn't find stream information." << endl;
		avformat_close_input(&pFormatCtx);//关闭已经打开的输入
		pFormatCtx = nullptr;
		return false;
	}

	//将有关文件的信息转存到标准错误（可选的调试）
	av_dump_format(pFormatCtx, 0, url, 0);

	//存储 URL统一资源定位器 、寻找音视频流
	m_url = url;
	findStreamsInternal();//查找并缓存流索引

	cout << "FFmpegDemuxer: Opened " << url << " successfully." << endl;
	if (m_videoStreamIndex != -1) {
		cout << " Video stream index: " << m_videoStreamIndex << endl;
	}
	if (m_audioStreamIndex != -1) {
		cout << " Audio stream index: " << m_audioStreamIndex << endl;
	}

	return true;
}

void FFmpegDemuxer::close() {
	if (pFormatCtx) {
		avformat_close_input(&pFormatCtx);
		// pFormatCtx 通过 avformat_close_input() 被设为空
		pFormatCtx = nullptr;
		m_videoStreamIndex = -1;
		m_audioStreamIndex = -1;
		m_url.clear();
		cout << "FFmpegDemuxer: Closed." << endl;
	}
}

int FFmpegDemuxer::readPacket(AVPacket* packet) {
	if (!pFormatCtx) {
		return AVERROR(EINVAL);//无效状态，没有打开
	}
	return av_read_frame(pFormatCtx, packet);//读取下一个 frame/packet
}

AVFormatContext* FFmpegDemuxer::getFormatContext() const {
	return pFormatCtx;
}

//查找流的辅助函数，由 open() 调用
void FFmpegDemuxer::findStreamsInternal() {
	if (!pFormatCtx) return;

	m_videoStreamIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	m_audioStreamIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

	//备用的线性查找方法，以防备 av_find_best_stream() 失败 或者 后继需要特定的逻辑
	//m_videoStreamIndex = -1;
	//m_audioStreamIndex = -1;
	//for (unsigned int i = 0; i < pFormatCtx->nb_streams; ++i) {
	//	if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_videoStreamIndex < 0) {
	//		m_videoStreamIndex = i;
	//	}
	//	else if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && m_audioStreamIndex < 0) {
	//		m_audioStreamIndex = i;
	//	}
	//	//优化：如果都找到了则退出break
	//	if (m_videoStreamIndex != -1 && m_audioStreamIndex != -1) {
	//		break;
	//	}
	//}
}

int FFmpegDemuxer::findStream(AVMediaType type) const {
	//返回缓存索引
	if (type == AVMEDIA_TYPE_VIDEO) {
		return m_videoStreamIndex;
	}
	else if (type == AVMEDIA_TYPE_AUDIO) {
		return m_audioStreamIndex;
	}
	//如果需要，在此添加其它类型（AVMEDIA_TYPE_SUBTITLE 或其它）
	return -1;//没找到类型或者没有缓存
}

AVCodecParameters* FFmpegDemuxer::getCodecParameters(int streamIndex)const {
	if (!pFormatCtx || streamIndex < 0 || streamIndex >= static_cast<int>(pFormatCtx->nb_streams)) {
		return nullptr;//无效的索引或者上下文
	}
	return pFormatCtx->streams[streamIndex]->codecpar;
}

/**
* @brief获取媒体文件的总时长（单位：秒）
* @return  double类型的总是从。若时长位置或者无法获取，返回0.0。
*/
double FFmpegDemuxer::getDuration() const {
	//1、检查 AVFormatContext 是否有效（pFormatCtx是在open()方法中初始化的）
	if (pFormatCtx) {
		//2、AVFormatContext::duration 字段存储了时长信息。
		//	这个值以 AV_TIEM_BASE 为单位（通常为微秒，1/1,000,000秒）。
		//	若时长未知，pFormatCtx->duration 可能是 AV_NOPTS_VALUE。
		if (pFormatCtx->duration != AV_NOPTS_VALUE) {
			//3、将时长从 AV_TIME_BASE 单位转为秒。
			//	AV_TIME_BASE 是一个宏，定义为 1000000.
			return static_cast<double>(pFormatCtx->duration) / AV_TIME_BASE;
		}
		else {
			//时长信息不可用
			//例如对于某些直播流，可能没有确定的总时长。
			//这种情况下，会考虑从流本身估算；但对于本最小可行播放器，返回0.0也可接受。
			//cerr << "FFmpegDemuxer : Duration is not available (AV_NOPTS_VALUE)." << endl;
			return 0.0;
		}
	}
	else {
		// AVFormatContext 未初始化（例如，open() 未调用或失败）
		//cerr << "FFmpegDemuxer : AVFormatContext is null, cannot get duration." << endl;
		return 0.0;
	}
}

AVRational FFmpegDemuxer::getTimeBase(int streamIndex) const {
	if (!pFormatCtx || streamIndex < 0 || streamIndex >= static_cast<int>(pFormatCtx->nb_streams)) {
		// 如果上下文无效或索引越界，返回一个无效的时间基
		return { 0, 1 };
	}
	// 正确地从 AVStream 结构体中获取 time_base
	return pFormatCtx->streams[streamIndex]->time_base;
}
