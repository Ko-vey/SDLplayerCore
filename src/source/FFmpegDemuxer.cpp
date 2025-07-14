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
#include <iostream> // ���ڴ�����Ϣ

using namespace std;

FFmpegDemuxer::FFmpegDemuxer() {
	// ���캯���������Ҫ���ڴ˳�ʼ�� ��ĳ�Ա
	// FFmpeg��ȫ�ֳ�ʼ������avformat_network_init()�ȣ�
	// �����������ط���ɣ���ÿ��Ӧ������ʱִֻ��һ��
}

FFmpegDemuxer::~FFmpegDemuxer() {
	//��������ȷ�������� close()
	close();
}

bool FFmpegDemuxer::open(const char* url) {
	//��ȷ���ر���ǰ��������
	close();

	pFormatCtx = avformat_alloc_context();
	if (!pFormatCtx) {
		cerr << "FFmpegDemuxer Error: Counld not allocate format context." << endl;
		return false;
	}

	//�������ļ�
	if (avformat_open_input(&pFormatCtx, url, nullptr, nullptr) != 0) {
		cerr << "FFmpegDemuxer Error: Counldn't open input stream: " << url << endl;
		avformat_free_context(pFormatCtx);//��Ҫ�ͷ���������������
		pFormatCtx = nullptr;
		return false;
	}

	//��������Ϣ
	if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
		cerr << "FFmpegDemuxer Error: Couldn't find stream information." << endl;
		avformat_close_input(&pFormatCtx);//�ر��Ѿ��򿪵�����
		pFormatCtx = nullptr;
		return false;
	}

	//���й��ļ�����Ϣת�浽��׼���󣨿�ѡ�ĵ��ԣ�
	av_dump_format(pFormatCtx, 0, url, 0);

	//�洢 URLͳһ��Դ��λ�� ��Ѱ������Ƶ��
	m_url = url;
	findStreamsInternal();//���Ҳ�����������

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
		// pFormatCtx ͨ�� avformat_close_input() ����Ϊ��
		pFormatCtx = nullptr;
		m_videoStreamIndex = -1;
		m_audioStreamIndex = -1;
		m_url.clear();
		cout << "FFmpegDemuxer: Closed." << endl;
	}
}

int FFmpegDemuxer::readPacket(AVPacket* packet) {
	if (!pFormatCtx) {
		return AVERROR(EINVAL);//��Ч״̬��û�д�
	}
	return av_read_frame(pFormatCtx, packet);//��ȡ��һ�� frame/packet
}

AVFormatContext* FFmpegDemuxer::getFormatContext() const {
	return pFormatCtx;
}

//�������ĸ����������� open() ����
void FFmpegDemuxer::findStreamsInternal() {
	if (!pFormatCtx) return;

	m_videoStreamIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	m_audioStreamIndex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

	//���õ����Բ��ҷ������Է��� av_find_best_stream() ʧ�� ���� �����Ҫ�ض����߼�
	//m_videoStreamIndex = -1;
	//m_audioStreamIndex = -1;
	//for (unsigned int i = 0; i < pFormatCtx->nb_streams; ++i) {
	//	if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_videoStreamIndex < 0) {
	//		m_videoStreamIndex = i;
	//	}
	//	else if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && m_audioStreamIndex < 0) {
	//		m_audioStreamIndex = i;
	//	}
	//	//�Ż���������ҵ������˳�break
	//	if (m_videoStreamIndex != -1 && m_audioStreamIndex != -1) {
	//		break;
	//	}
	//}
}

int FFmpegDemuxer::findStream(AVMediaType type) const {
	//���ػ�������
	if (type == AVMEDIA_TYPE_VIDEO) {
		return m_videoStreamIndex;
	}
	else if (type == AVMEDIA_TYPE_AUDIO) {
		return m_audioStreamIndex;
	}
	//�����Ҫ���ڴ�����������ͣ�AVMEDIA_TYPE_SUBTITLE ��������
	return -1;//û�ҵ����ͻ���û�л���
}

AVCodecParameters* FFmpegDemuxer::getCodecParameters(int streamIndex)const {
	if (!pFormatCtx || streamIndex < 0 || streamIndex >= static_cast<int>(pFormatCtx->nb_streams)) {
		return nullptr;//��Ч����������������
	}
	return pFormatCtx->streams[streamIndex]->codecpar;
}

/**
* @brief��ȡý���ļ�����ʱ������λ���룩
* @return  double���͵����Ǵӡ���ʱ��λ�û����޷���ȡ������0.0��
*/
double FFmpegDemuxer::getDuration() const {
	//1����� AVFormatContext �Ƿ���Ч��pFormatCtx����open()�����г�ʼ���ģ�
	if (pFormatCtx) {
		//2��AVFormatContext::duration �ֶδ洢��ʱ����Ϣ��
		//	���ֵ�� AV_TIEM_BASE Ϊ��λ��ͨ��Ϊ΢�룬1/1,000,000�룩��
		//	��ʱ��δ֪��pFormatCtx->duration ������ AV_NOPTS_VALUE��
		if (pFormatCtx->duration != AV_NOPTS_VALUE) {
			//3����ʱ���� AV_TIME_BASE ��λתΪ�롣
			//	AV_TIME_BASE ��һ���꣬����Ϊ 1000000.
			return static_cast<double>(pFormatCtx->duration) / AV_TIME_BASE;
		}
		else {
			//ʱ����Ϣ������
			//�������ĳЩֱ����������û��ȷ������ʱ����
			//��������£��ῼ�Ǵ���������㣻�����ڱ���С���в�����������0.0Ҳ�ɽ��ܡ�
			//cerr << "FFmpegDemuxer : Duration is not available (AV_NOPTS_VALUE)." << endl;
			return 0.0;
		}
	}
	else {
		// AVFormatContext δ��ʼ�������磬open() δ���û�ʧ�ܣ�
		//cerr << "FFmpegDemuxer : AVFormatContext is null, cannot get duration." << endl;
		return 0.0;
	}
}

AVRational FFmpegDemuxer::getTimeBase(int streamIndex) const {
	if (!pFormatCtx || streamIndex < 0 || streamIndex >= static_cast<int>(pFormatCtx->nb_streams)) {
		// �����������Ч������Խ�磬����һ����Ч��ʱ���
		return { 0, 1 };
	}
	// ��ȷ�ش� AVStream �ṹ���л�ȡ time_base
	return pFormatCtx->streams[streamIndex]->time_base;
}
