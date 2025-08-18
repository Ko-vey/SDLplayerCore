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

struct AVFormatContext;
struct AVPacket;
struct AVCodecParameters;
struct AVRational;
enum AVMediaType;	// enum

class IDemuxer {
public:
	// 为虚析构函数提供默认的标准实现
	virtual ~IDemuxer() = default;

	/**
	 * @brief 打开媒体源（文件路径）
	 * @param 
	 * @return 成功返回true，失败返回false
	 */
	virtual bool open(const char* url) = 0;

	/**
	 * @brief 关闭媒体源、释放资源
	 */
	virtual void close() = 0;

	/**
	 * @brief 读取下一个数据包
	 * @param packet 调用者提供的 AVPacket 结构体指针，用于接收数据
	 * @return 成功返回0，文件结束返回AVERROR_EOF，其他错误则返回<0的数字
	 */
	virtual int readPacket(AVPacket* packet) = 0;

	/**
	 * @brief 获取底层的 AVFormatContext（用于获取更详细信息）
	 * 注意：通常应谨慎暴露底层上下文，但有时有必要与其它FFmpeg组件交互
	 * @return 指向 AVFormatContext 的指针。若未打开则为 nullptr
	 */
	virtual AVFormatContext* getFormatContext() const = 0;

	/**
	 * @brief 查找指定媒体类型的流索引
	 * @param type AVMEDIA_TYPE_VIDEO, AVMEDIA_TYPE_AUDIO, etc.
	 * @return 流索引（>=0）,若未找到则返回 -1
	 */
	virtual int findStream(AVMediaType type) const = 0;

	/**
	 * @brief 获取指定流的编解码器参数
	 * @param streamIndex 流的索引
	 * @return 指向 AVCodecParameters 的指针，若索引无效则为 nullptr
	 */
	virtual AVCodecParameters* getCodecParameters(int streamIndex)const = 0;

	/**
	 * @brief 获取媒体文件的总时长
	 */
	virtual double getDuration() const = 0;

	/**
	 * @brief 获取指定流的时间基 (time_base).
	 * @param streamIndex 流的索引.
	 * @return AVRational 结构体，表示时间基。若索引无效则返回 {0, 1}。
	 */
	virtual AVRational getTimeBase(int streamIndex) const = 0;
	
	//（其他功能，如获取元数据等）
	//virtual AVDictionary* getMetadata() const = 0;
};
