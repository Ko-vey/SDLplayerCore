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

#pragma once

#include "../include/IDemuxer.h"
#include <string>

using namespace std;

//包含用于实现的实际FFmpeg头文件
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>	// AVCodecParameters, AVMediaType
#include <libavutil/dict.h>		// 元数据功能
#include <libavutil/time.h>		// AV_TIME_BASE
}

class FFmpegDemuxer : public IDemuxer {
private:
	AVFormatContext* pFormatCtx = nullptr;
	string m_url;
	int m_videoStreamIndex = -1;
	int m_audioStreamIndex = -1;

public:
	FFmpegDemuxer();
	virtual ~FFmpegDemuxer() override;

	// IDemuxer 接口实现
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
