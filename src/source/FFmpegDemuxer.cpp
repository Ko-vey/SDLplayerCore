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
#include <iostream>

using namespace std;

FFmpegDemuxer::~FFmpegDemuxer() {
	close();
}

// 中断回调函数
int FFmpegDemuxer::interruptCallback(void* opaque) {
	auto demuxer = static_cast<FFmpegDemuxer*>(opaque);
	if (demuxer && demuxer->m_abort_request.load()) {
		// 如果 m_abort_request 为 true，返回 1 表示中断
		std::cout << "FFmpegDemuxer: Interrupt requested." << std::endl;
		return 1;
	}
	// 否则返回 0，继续执行
	return 0;
}

// 请求中断的公共方法
void FFmpegDemuxer::requestAbort(bool abort) {
	m_abort_request.store(abort);
}

bool FFmpegDemuxer::open(const char* url) {
	// 确保关闭先前的上下文
	close();

	// 初始化状态
	m_isLiveStream = false; // 默认先重置为点播模式
	m_abort_request.store(false); // 重置中断标志

	pFormatCtx = avformat_alloc_context();
	if (!pFormatCtx) {
		cerr << "FFmpegDemuxer Error: Could not allocate format context." << endl;
		return false;
	}

	// 设置中断回调
	pFormatCtx->interrupt_callback.callback = FFmpegDemuxer::interruptCallback;
	pFormatCtx->interrupt_callback.opaque = this; // 将当前对象实例作为上下文传递

	// 设置网络选项
	AVDictionary* opts = nullptr;
	// 1. 设置RTSP传输协议为TCP。FFmpeg默认可能尝试UDP，在某些网络下可能失败。
	av_dict_set(&opts, "rtsp_transport", "tcp", 0);
	// 2. 设置超时时间（单位：微秒）。此处设置5秒超时，防止网络卡顿时程序假死。
	av_dict_set(&opts, "stimeout", "5000000", 0);
	
	// 打开输入流，并传入刚刚设置的选项
	int ret = avformat_open_input(&pFormatCtx, url, nullptr, &opts);
	// 检查是否有未使用的选项（用于调试），并释放字典
	if (opts) {
		char* value = nullptr;
		if (av_dict_get(opts, "", nullptr, AV_DICT_IGNORE_SUFFIX)) {
			// 可以选择打印未被识别的选项，帮助排查拼写错误
			// cout << "FFmpegDemuxer: Some options were not used." << endl;
		}
		av_dict_free(&opts);
	}

	if (ret != 0) {
		// 错误处理
		char errbuf[1024] = { 0 };
		av_strerror(ret, errbuf, sizeof(errbuf));
		cerr << "FFmpegDemuxer Error: Couldn't open input stream: " << url << " (" << errbuf << ")" << endl;
		avformat_free_context(pFormatCtx);
		pFormatCtx = nullptr;
		return false;
	}

	// 检索流信息
	if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
		cerr << "FFmpegDemuxer Error: Couldn't find stream information." << endl;
		avformat_close_input(&pFormatCtx);
		pFormatCtx = nullptr;
		return false;
	}

	// 将有关文件的信息转存到标准错误
	av_dump_format(pFormatCtx, 0, url, 0);

	m_url = url;
	findStreamsInternal();

	// --- 检测是否为直播流 ---
	if (pFormatCtx) {
		std::string formatName = pFormatCtx->iformat->name ? pFormatCtx->iformat->name : "";
		std::string urlStr = url;

		// 协议/格式名称强匹配
		if (formatName == "rtsp" || formatName == "flv" || formatName == "hls" || formatName == "rtp") {
			// 注意：flv和hls也可以是点播，但通常作为直播处理逻辑（允许丢包）更安全
			// 如果确定是文件型flv，通常有duration
			m_isLiveStream = true;
		}
		// URL 协议头检查
		else if (urlStr.find("rtsp://") == 0 || urlStr.find("rtmp://") == 0 || urlStr.find("udp://") == 0) {
			m_isLiveStream = true;
		}
		// 特征检查：无时长 且 不可Seek
		// 注意：有些直播流可能有极其巨大的 duration (INT64_MAX)，或者 0
		else if (pFormatCtx->duration == AV_NOPTS_VALUE) {
			m_isLiveStream = true;
		}
		// IO Context 检查
		else if (pFormatCtx->pb && !(pFormatCtx->pb->seekable & AVIO_SEEKABLE_NORMAL)) {
			// 如果底层IO不支持seek，通常认为是直播流
			m_isLiveStream = true;
		}
	}

	cout << "FFmpegDemuxer: Opened " << url << " successfully." << endl;
	if (m_videoStreamIndex != -1) {
		cout << " Video stream index: " << m_videoStreamIndex << endl;
	}
	if (m_audioStreamIndex != -1) {
		cout << " Audio stream index: " << m_audioStreamIndex << endl;
	}
	cout << "FFmpegDemuxer: Stream is detected as: " << (m_isLiveStream ? "LIVE" : "VOD/LOCAL") << endl;

	return true;
}

void FFmpegDemuxer::close() {
	requestAbort(true); // 在关闭前，先请求中断，以防有线程卡在读取操作上
	if (pFormatCtx) {
		avformat_close_input(&pFormatCtx);
		pFormatCtx = nullptr;
		m_videoStreamIndex = -1;
		m_audioStreamIndex = -1;
		m_url.clear();
		cout << "FFmpegDemuxer: Closed." << endl;
	}
}

int FFmpegDemuxer::readPacket(AVPacket* packet) {
	if (!pFormatCtx) {
		return AVERROR(EINVAL); // 无效状态，没有打开
	}
	return av_read_frame(pFormatCtx, packet); // 读取下一个 frame/packet
}

int FFmpegDemuxer::seek(double timestamp_sec) {
	if (!pFormatCtx) return -1;

	// 将秒转换为默认流的时间基单位
	// -1 表示使用默认流，通常是视频流
	int64_t seek_target_ts = static_cast<int64_t>(timestamp_sec * AV_TIME_BASE);

	// AVSEEK_FLAG_BACKWARD: 允许向后seek，对于seek到0或开头是必要的
	int ret = av_seek_frame(pFormatCtx, -1, seek_target_ts, AVSEEK_FLAG_BACKWARD);

	if (ret < 0) {
		char errbuf[AV_ERROR_MAX_STRING_SIZE];
		av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, ret);
		std::cerr << "FFmpegDemuxer: Failed to seek: " << errbuf << std::endl;
	}
	else {
		std::cout << "FFmpegDemuxer: Seek to " << timestamp_sec << "s successful." << std::endl;
	}
	return ret;
}

AVFormatContext* FFmpegDemuxer::getFormatContext() const {
	return pFormatCtx;
}

// 查找流的辅助函数，由 open() 调用
void FFmpegDemuxer::findStreamsInternal() {
	if (!pFormatCtx) return;

	m_videoStreamIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	m_audioStreamIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
}

int FFmpegDemuxer::findStream(AVMediaType type) const {
	// 返回缓存索引
	if (type == AVMEDIA_TYPE_VIDEO) {
		return m_videoStreamIndex;
	}
	else if (type == AVMEDIA_TYPE_AUDIO) {
		return m_audioStreamIndex;
	}
	//如果需要，在此添加其它类型（AVMEDIA_TYPE_SUBTITLE 或其它）
	return -1; // 没找到类型或者没有缓存
}

AVCodecParameters* FFmpegDemuxer::getCodecParameters(int streamIndex)const {
	if (!pFormatCtx || streamIndex < 0 || streamIndex >= static_cast<int>(pFormatCtx->nb_streams)) {
		return nullptr; // 无效的索引或者上下文
	}
	return pFormatCtx->streams[streamIndex]->codecpar;
}

double FFmpegDemuxer::getDuration() const {
	//1、检查 AVFormatContext 是否有效（pFormatCtx在open()方法中初始化）
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
			// 时长信息不可用，
			// 例如对于某些直播流，可能没有确定的总时长。
			// 这种情况下，会考虑从流本身估算；但考虑最小可行，返回0.0也可接受。
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

bool FFmpegDemuxer::isLiveStream() const {
	return m_isLiveStream; 
}
