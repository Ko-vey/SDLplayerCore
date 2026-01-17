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

#include "../include/FFmpegVideoDecoder.h"
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>		// av_err2str
//#include <libavutil/opt.h>		// 设置解码器选项（如线程数）
}

using namespace std;

FFmpegVideoDecoder::FFmpegVideoDecoder() {}

FFmpegVideoDecoder::~FFmpegVideoDecoder() {
	close();
}

bool FFmpegVideoDecoder::init(AVCodecParameters* codecParams, AVRational timeBase) {
	if (!codecParams) {
		cerr << "FFmpegVideoDecoder::init Error: codecParams is null." << endl;
		return false;
	}

	// 如果已经初始化，先关闭旧的上下文
	if (m_codecContext) {
		cout << "FFmpegVideoDecoder::init warning: Decoder already initialized. Closing previous instance." << endl;
		close();
	}

	//1、查找解码器
	const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
	if (!codec) {
		cerr << "FFmpegVideoDecoder::init Error: Decoder not found for codec ID " << codecParams->codec_id
			<< " (" << avcodec_get_name(codecParams->codec_id) << ")." << endl;
		return false;
	}

	//2、分配解码器上下文
	m_codecContext = avcodec_alloc_context3(codec);
	if (!m_codecContext) {
		cerr << "FFmpegVideoDecoder::init Error: Failed to allocate AVCodecContext." << endl;
		return false;
	}

	// 3、拷贝编解码器参数到上下文
	if (avcodec_parameters_to_context(m_codecContext, codecParams) < 0) {
		cerr << "FFmpegVideoDecoder::init Error: Could not copy codec parameters to context." << endl;
		avcodec_free_context(&m_codecContext);
		return false;
	}

	// 手动设置时间基
	m_codecContext->time_base = timeBase;

	// 启用多线程解码（具体线程数可以配置，0 为自动）
	//m_codecContext->thread_count = 0;
	//m_codecContext->thread_type = FF_THREAD_FRAME;	// 或者 FF_THREAD_SLICE

	// 4、打开解码器
	if (avcodec_open2(m_codecContext, codec, nullptr) < 0) {
		cerr << "FFmpegVideoDecoder::init Error: Could not open codec (" << codec->long_name << endl;
		avcodec_free_context(&m_codecContext);
		return false;
	}

	cout << "FFmpegVideoDecoder initialized successfully with codec: " << codec->long_name << ", TimeBase: " << timeBase.num << "/" << timeBase.den << endl;
	return true;
}

int FFmpegVideoDecoder::decode(AVPacket* packet, AVFrame** frame) {
	if (!m_codecContext || m_codecContext->codec_id == AV_CODEC_ID_NONE) {
		cerr << "FFmpegVideoDecoder::decode Error: Decoder not initialized or has been closed." << endl;
		return AVERROR(EINVAL); // 无效参数或状态
	}
	if (!frame) {
		cerr << "FFmpegVideoDecoder::decode Error: Output frame pointer (frame) is null." << endl;
		return AVERROR(EINVAL);
	}
	*frame = nullptr; // 确保输出参数被初始化0

	// 步骤 1： 发送数据包到解码器
	// packet 为 nullptr 表示冲洗解码器（发送EOF信号）
	int ret = avcodec_send_packet(m_codecContext, packet);

	if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
		// 发生了一个不可恢复的发送错误
		char errbuf[AV_ERROR_MAX_STRING_SIZE];
		av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, ret);
		cerr << "FFmpegVideoDecoder::decode Error: Failed to send packet to decoder: " << errbuf<< endl;
		return ret;
	}
	// 如果 ret == AVERROR(EAGAIN)，表示解码器内部缓冲区已满，不能再接收输入，直到有帧被取出。
	// 此时仍然应该尝试接收帧。
	// 如果 ret == AVERROR_EOF，表示解码器已经被冲洗，不能再发送新的包（除非是nullptr）。
	// 如果 packet 非空但发送返回 AVERROR_EOF，这通常是个错误用法。
	// 但接口设计是先send再receive，所以让receive来处理最终状态。

	//步骤 2：从解码器接收解码后的帧
	AVFrame* decoded_frame = av_frame_alloc();
	if (!decoded_frame) {
		cerr << "FFmpegVideoDecoder::decode Error: Failed to allocate AVFrame." << endl;
		return AVERROR(ENOMEM);	// 内存不足
	}

	// avcodec 内部自动完成 PTS 和 DTS 的顺序协调
	ret = avcodec_receive_frame(m_codecContext, decoded_frame);

	if (ret == 0) { // 成功接收到一帧
		*frame = decoded_frame;	//将分配的帧的所有权转移给调用者
		return 0;
	}
	else {
		av_frame_free(&decoded_frame);	// 如果没有成功接收到帧，释放已经分配的 AVFrame
		*frame = nullptr;
		// 如果 ret 是 AVERROR(EAGAIN)（需要更多输入）或 AVERROR_EOF（流结束，没有更多帧），
		// 这些是预期的返回值，不一定是“错误”。
		// 其它负值表示解码错误。
		if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
			char errbuf[AV_ERROR_MAX_STRING_SIZE];
			av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, ret);
			cerr << "FFmpegVideoDecoder::decode Error: Failed to receive frame from decoder: " << errbuf << endl;
		}
		return ret;
	}
}

void FFmpegVideoDecoder::close() {
	if (m_codecContext) {
		avcodec_free_context(&m_codecContext);	// 释放上下文内存，m_codecContext 会被置为 nullptr
		cout << "FFmpegVideoDecoder::close: Codec context closed and freed." << endl;
	}
}

void FFmpegVideoDecoder::flush() {
	if (m_codecContext) {
		avcodec_flush_buffers(m_codecContext);
	}
}

int FFmpegVideoDecoder::getWidth() const {
	return m_codecContext ? m_codecContext->width : 0;
}

int FFmpegVideoDecoder::getHeight() const {
	return m_codecContext ? m_codecContext->height : 0;
}

AVPixelFormat FFmpegVideoDecoder::getPixelFormat() const {
	return m_codecContext ? m_codecContext->pix_fmt : AV_PIX_FMT_NONE;
}

AVRational FFmpegVideoDecoder::getTimeBase()const {
	if (m_codecContext) {
		// 解码器上下文的时间基准通常从输入流复制过来
		return m_codecContext->time_base;
	}
	return { 0,1 };	// 返回一个表示无效或未初始化的时间基准
}

AVRational FFmpegVideoDecoder::getFrameRate()const {
	if (m_codecContext) {
		// AVCodecContext->framerate 是解码器认为的帧率
		return m_codecContext->framerate;
	}
	return { 0,1 };	// 返回一个表示无效或未初始化的帧率
}
