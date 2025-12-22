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

#include "IClockManager.h"	// 时钟管理器接口；音频解码出的帧的PTS用于后续更新时钟

struct AVCodecParameters;	// 编解码器参数结构体
struct AVPacket;			// 数据包结构体
struct AVFrame;				// 数据帧结构体
enum AVSampleFormat;		// 采样格式枚举
struct AVRational;			// 有理数结构体（用于时间基准）

class IAudioDecoder {
public:
	virtual ~IAudioDecoder() = default;

	/**
	* @brief 使用给定的编解码器参数和时间基来初始化音频解码器。
	* @param codecParams 指向音频流的 AVCodecParameters 的指针。
	* @param timeBase 音频流的时间基。
	* @param clockManager 指向时钟管理器的指针。
	* @return 若初始化成功返回 true，否则返回 false。
	*/
	virtual bool init(AVCodecParameters* codecParams, AVRational timeBase, IClockManager* clockManager) = 0;

	/**
	* @brief 将单个音频包解码为一个音频帧（PCM数据）。
	* @param packet 包含待解码的压缩音频数据的 AVPacket。
	* @param frame 指向 AVFrame 指针的指针，该指针将被解码后的PCM数据填充，
	* @return 成功时返回0（帧已解码），若需要更多输入则返回 AVERROR(EAGAIN)，
	* 若解码器已完全刷新、且不会再有更多帧输出则返回AVERROR_EOF，
	* 失败时返回负的错误代码。
	*/
	virtual int decode(AVPacket* packet, AVFrame** frame) = 0;

	/**
	* @brief 关闭解码器并释放所有相关资源。
	* 调用此方法后，解码器实例将不可用。
	*/
	virtual void close() = 0;

	/*
	* @brief 刷新模块内数据。
	*/
	virtual void flush() = 0;

	/**
	* @brief 获取解码后的音频采样率（如44.1kHz、48kHz）。
	* @return 采样率。
	*/
	virtual int getSampleRate() const = 0;

	/**
	* @brief 获取解码后的音频的声道数（如 1-单声道，2-立体声道）
	* @return 声道数
	*/
	virtual int getChannels() const = 0;

	/**
	* @brief 获取解码后音频的采样格式（如 AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLTP)。
	* 对于音频渲染器配置音频设备非常重要。
	* @return AVSampleFormat 枚举值。
	*/
	virtual enum AVSampleFormat getSampleFormat() const = 0;

	/**
	* @brief 获取音频流的时间基准（time base）。
	* 时间基准定义了时间戳的基本单位。
	* @return 表示时间基准的 AVRational 结构体。
	*/
	virtual struct AVRational getTimeBase() const = 0;

	// 获取每个采样点的字节数或每个完整采样帧的字节数
	//（通常可以从 sample_format 和 channels 推断出来，或者由音频渲染器根据这些参数计算）
	virtual int getBytesPerSampleFrame() const = 0;
};
