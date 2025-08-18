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

#include <string>

struct AVCodecParameters;	// 编解码器参数结构体
struct AVPacket;			// 数据包结构体
struct AVFrame;				// 数据帧结构体
enum AVPixelFormat;			// 像素格式枚举，用于getPixelFormat()
struct AVRational;			// FFmpeg中用于表示有理数的结构（如时间基准、帧率）

class IVideoDecoder {
public:
	virtual ~IVideoDecoder() = default;

	/**
	* @brief 使用给定的编解码器参数，初始化视频解码器
	* @param codecParams 指向从解封装器获取的视频流的 AVCodecParameters 的指针
	* @return 若初始化成功返回true，否则返回false。
	*/
	virtual bool init(AVCodecParameters* codecParams) = 0;

	/**
	* @brief 将单个视频包解码为一个视频帧。
	* 调用者负责管理packet和frame的生命周期。
	* @param packet 包含待解码的压缩视频数据的 AVPacket。
	* @param frame 指向 AVFrame 指针的指针，该指针将被解码后的视频数据填充，
	* 解码器会从其内部池中分配/获取一个帧。
	* @return 成功时返回0（帧已解码），若需要更多输入则返回 AVERROR(EAGAIN)，
	* 若到达流末尾则返回AVERROR_EOF，失败时返回负的错误代码。
	*/
	virtual int decode(AVPacket* packet, AVFrame** frame) = 0;

	/**
	* @brief 关闭解码器并释放所有相关资源。
	* 调用此方法后，解码器实例将不可用。
	*/
	virtual void close() = 0;

	/**
	* @brief 获取解码后的视频帧的宽度
	* @return 宽度（单位：像素）。
	*/
	virtual int getWidth() const = 0;

	/**
	* @brief 获取解码后的视频帧的高度
	* @return 高度（单位：像素）。
	*/
	virtual int getHeight() const = 0;

	/**
	* @brief 获取解码后的视频帧的像素格式
	* @return AVPixelFormat 枚举值。
	*/
	virtual AVPixelFormat getPixelFormat() const = 0;

	/**
	* @brief 获取视频流的时间基准（time base）。
	* 时间基准定义了时间戳的基本单位。
	* @return 表示时间基准的 AVRational 结构体。
	*/
	virtual struct AVRational getTimeBase() const = 0;

	/**
	* @brief 获取视频流的平均帧率。
	* @return 表示平均帧率的 AVRational 结构体。
	*/
	virtual struct AVRational getFrameRate() const = 0;
};
