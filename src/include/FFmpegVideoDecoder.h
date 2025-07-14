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

#include "IVideoDecoder.h"

// 前向声明 FFmpeg 类型，避免在头文件中包含庞大的FFmpeg头文件
struct AVCodecContext;

class FFmpegVideoDecoder : public IVideoDecoder {
public:
	FFmpegVideoDecoder();
	~FFmpegVideoDecoder() override;

	//禁止拷贝构造函数和赋值操作符
	FFmpegVideoDecoder(const FFmpegVideoDecoder&) = delete;
	FFmpegVideoDecoder& operator=(const FFmpegVideoDecoder&) = delete;

	/**
	* @brief 使用给定的编解码器参数和时钟管理器 初始化视频解码器。
	* @param codecParams 指向从解复用器获取的视频流的 AVCodecParameters 的指针。
	* @return 若初始化成功返回 true，否则返回 false。
	*/
	bool init(AVCodecParameters* codecParams) override;

	/**
	* @brief 将单个视频包解码为一个或多个视频帧。
	* 调用者负责管理 packet 的生命周期。
	* 如果解码成功，函数会分配一个新的 AVFrame 并通过 frame 参数返回。调用者在处理完该 frame 后，
	* 负责调用 av_frame_free() 来释放它。
	* @param packet 包含待解码的压缩视频数据的 AVPacket。
	* 若为 nullptr，则表示冲洗解码器（获取内部缓冲的帧）。
	* @param frame 指向 AVFrame 指针的指针。如果解码成功，*frame 将指向新分配的解码帧。
	* 如果解码失败或需要更多输入，*frame将被设为 nullptr。
	* @return 成功时返回 0（帧已解码并置于 *frame），
	* 若需要更多输入则返回 AVERROR(EAGAIN)（此时 *frame 为nullptr
	* 若到达流末尾则返回 AVERROR_EOF（此时 *frame 为 nullptr），
	* 其它负值表示解码错误。
	*/
	int decode(AVPacket* packet, AVFrame** frame) override;

	/**
	* @brief 关闭解码器并释放所有相关 FFmpeg 资源。
	* 调用此方法后，解码器实例将不可用，除非再次调用 init()。
	*/
	void close() override;

	/**
	* @brief 获取解码后的视频帧的宽度。
	* @return 宽度（单位：像素），如果解码器未初始化则返回 0。
	*/
	int getWidth() const override;
	
	/**
	* @brief 获取解码后的视频帧的高度。
	* @return 高度（单位：像素），如果解码器未初始化则返回 0。
	*/
	int getHeight() const override;

	/**
	* @brief 获取解码后的视频帧的像素格式。
	* @return AVPixelFormat 枚举值，如果解码器未初始化则返回 AV_PIX_FMT_NONE。
	*/
	AVPixelFormat getPixelFormat() const override;

	/**
	* @brief 获取视频流的时间基准。
	* @return 表示时间基准的 AVRational 结构体，如果解码器未初始化则返回{0,1}。
	*/
	AVRational getTimeBase() const override;
	
	/**
	* @brief 获取视频流的平均帧率。
	* @return 表示平均帧率的 AVRational 结构体，如果解码器未初始化则返回{0,1}。
	*/
	AVRational getFrameRate() const override;

private:
	AVCodecContext* m_codecContext;	// FFmpeg 解码器上下文
};
