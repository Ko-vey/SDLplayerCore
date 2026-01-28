/*
 * SDLplayerCore - An audio and video player.
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

#include "IVideoDecoder.h"

struct AVCodecContext;

class FFmpegVideoDecoder : public IVideoDecoder {
public:
	FFmpegVideoDecoder();
	~FFmpegVideoDecoder() override;

	FFmpegVideoDecoder(const FFmpegVideoDecoder&) = delete;
	FFmpegVideoDecoder& operator=(const FFmpegVideoDecoder&) = delete;

	bool init(AVCodecParameters* codecParams, AVRational timeBase) override;
	int decode(AVPacket* packet, AVFrame** frame) override;
	void close() override;
	void flush() override;

	int getWidth() const override;
	int getHeight() const override;
	AVPixelFormat getPixelFormat() const override;
	AVRational getTimeBase() const override;
	AVRational getFrameRate() const override;

	AVCodecID getCodecID() const override;

private:
	AVCodecContext* m_codecContext = nullptr;
};
