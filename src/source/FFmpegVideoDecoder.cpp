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
#include <iostream>	//�������std::cerr��std::cout

// ���� FFmpeg C����ͷ�ļ�
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>	// ���� av_err2str
//#include <libavutil/opt.h>		//�����Ҫ���ý�����ѡ����߳�����
}

using namespace std;

FFmpegVideoDecoder::FFmpegVideoDecoder() : m_codecContext(nullptr) {
	cout << "FFmpegVideoDecoder instance created." << endl;
}

FFmpegVideoDecoder::~FFmpegVideoDecoder() {
	cout << "FFmpegVideoDecoder instance being destroyed." << endl;
	close();	//ȷ����Դ������ʱ���ͷ�
}

bool FFmpegVideoDecoder::init(AVCodecParameters* codecParams) {
	if (!codecParams) {
		cerr << "FFmpegVideoDecoder::init Error: codecParams is null." << endl;
		return false;
	}

	// ����Ѿ���ʼ�����ȹرվɵ�������
	if (m_codecContext) {
		cout << "FFmpegVideoDecoder::init warning: Decoder already initialized. Closing previous instance." << endl;
		close();
	}

	//1�����ҽ�����
	const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
	if (!codec) {
		cerr << "FFmpegVideoDecoder::init Error: Decoder not found for codec ID " << codecParams->codec_id
			<< " (" << avcodec_get_name(codecParams->codec_id) << ")." << endl;
		return false;
	}

	//2�����������������
	m_codecContext = avcodec_alloc_context3(codec);
	if (!m_codecContext) {
		cerr << "FFmpegVideoDecoder::init Error: Failed to allocate AVCodecContext." << endl;
		return false;
	}

	// 3�������������������������
	if (avcodec_parameters_to_context(m_codecContext, codecParams) < 0) {
		cerr << "FFmpegVideoDecoder::init Error: Could not copy codec parameters to context." << endl;
		avcodec_free_context(&m_codecContext);//�����ѷ����������
		return false;
	}

	//ʾ�������ö��߳̽��루�����߳����������ã�0 Ϊ�Զ���
	//m_codecContext->thread_count = 0;
	//m_codecContext->thread_type = FF_THREAD_FRAME;	// ���� FF_THREAD_SLICE

	// 4���򿪽�����
	if (avcodec_open2(m_codecContext, codec, nullptr) < 0) {
		cerr << "FFmpegVideoDecoder::init Error: Could not open codec (" << codec->long_name << endl;
		avcodec_free_context(&m_codecContext);//�����ѷ����������
		return false;
	}

	cout << "FFmpegVideoDecoder initialized successfully with codec: " << codec->long_name << endl;
	return true;
}

int FFmpegVideoDecoder::decode(AVPacket* packet, AVFrame** frame) {
	if (!m_codecContext || m_codecContext->codec_id == AV_CODEC_ID_NONE) {
		cerr << "FFmpegVideoDecoder::decode Error: Decoder not initialized or has been closed." << endl;
		return AVERROR(EINVAL);//��Ч������״̬
	}
	if (!frame) {
		cerr << "FFmpegVideoDecoder::decode Error: Output frame pointer (frame) is null." << endl;
		return AVERROR(EINVAL);
	}
	*frame = nullptr; // ȷ�������������ʼ��

	// ���� 1�� �������ݰ���������
	// packet Ϊ nullptr ��ʾ��ϴ������������EOF�źţ�
	int ret = avcodec_send_packet(m_codecContext, packet);

	if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
		// ������һ�����ɻָ��ķ��ʹ���
		char errbuf[AV_ERROR_MAX_STRING_SIZE];
		av_make_error_string(errbuf, AV_ERROR_MAX_STRING_SIZE, ret);
		cerr << "FFmpegVideoDecoder::decode Error: Failed to send packet to decoder: " << errbuf<< endl;
		return ret;
	}
	// ��� ret == AVERROR(EAGAIN)����ʾ�������ڲ������������������ٽ������룬ֱ����֡��ȡ����
	// ��ʱ��ȻӦ�ó��Խ���֡��
	// ��� ret == AVERROR_EOF����ʾ�������Ѿ�����ϴ�������ٷ����µİ���������nullptr����
	// ��� packet �ǿյ����ͷ��� AVERROR_EOF����ͨ���Ǹ������÷���
	// ���ӿ��������send��receive������������receive����������״̬��

	//���� 2���ӽ��������ս�����֡
	AVFrame* decoded_frame = av_frame_alloc();
	if (!decoded_frame) {
		cerr << "FFmpegVideoDecoder::decode Error: Failed to allocate AVFrame." << endl;
		return AVERROR(ENOMEM);	// �ڴ治��
	}

	// avcodec �ڲ��Զ���� PTS �� DTS ��˳��Э��
	ret = avcodec_receive_frame(m_codecContext, decoded_frame);

	if (ret == 0) { // �ɹ����յ�һ֡
		*frame = decoded_frame;	//�������֡������Ȩת�Ƹ�������
		return 0;
	}
	else {
		av_frame_free(&decoded_frame);	//���û�гɹ����յ�֡���ͷ��Ѿ������ AVFrame
		*frame = nullptr;
		// ��� ret �� AVERROR(EAGAIN)����Ҫ�������룩�� AVERROR_EOF����������û�и���֡����
		// ��Щ��Ԥ�ڵķ���ֵ����һ���ǡ����󡱡�
		// ������ֵ��ʾ�������
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
		avcodec_free_context(&m_codecContext);	// Ȼ���ͷ��������ڴ棬m_codecContext �ᱻ��Ϊ NULL
		cout << "FFmpegVideoDecoder::close: Codec context closed and freed." << endl;
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
		//�����������ĵ�ʱ���׼ͨ���Ǵ����������ƹ�����
		return m_codecContext->time_base;
	}
	return { 0,1 };	// ����һ����ʾ��Ч��δ��ʼ����ʱ���׼
}

AVRational FFmpegVideoDecoder::getFrameRate()const {
	if (m_codecContext) {
		// AVCodecContext->framerate �ǽ�������Ϊ��֡�ʣ�
		// ��Ӧ���Ǵ� AVStream->avg_frame_rate �� AVStream->r_frame_rate ��ʼ���õ��ġ�
		return m_codecContext->framerate;
	}
	return { 0,1 };	// ����һ����ʾ��Ч��δ��ʼ����֡��
}
