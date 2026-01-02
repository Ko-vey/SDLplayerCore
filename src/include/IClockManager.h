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

#include "SDL2/SDL_audio.h"	// SDL_AudioDeviceID

// 确切的时钟类型
enum class MasterClockType {
	AUDIO,	 // 0
	EXTERNAL // 1
};

class IClockManager {
public:
	virtual ~IClockManager() = default;

	/**
	* @brief 初始化时钟管理器。
	* @param has_audio 存在音频流与否。
	* @param has_video 存在视频流与否。
	*/
	virtual void init(bool has_audio, bool has_video) = 0;

	/**
	* @brief 设置确切的时钟类型。
	* @param type 时钟类型。
	*/
	virtual void setMasterClock(MasterClockType type) = 0;

	/**
	* @brief 获取当前的主时钟类型。
	*/
	virtual MasterClockType getMasterClockType() const = 0;

	/**
	* @brief 获取主时钟的当前时间（单位：秒）。
	* 实现类内部会根据是否有音频等情况决定返回哪个时钟值。
	* @return 主时钟的当前时间。
	*/
	virtual double getMasterClockTime() = 0;

	/**
	 * @brief 更新音频时钟的当前时间（单位：秒）
	 * @warning 为了计算精确，务必在将音频数据写入 SDL 队列后调用此函数。
	 * 传入的 pts 应当是【刚刚写入队列的那段音频数据的结束时间】。
	 * 即：Current_Packet_Start_PTS + Current_Packet_Duration。
	 */
	virtual void setAudioClock(double pts) = 0;

	/**
	* @brief 获取音频时钟的当前时间（单位：秒）
	* @return 音频时钟的时间。
	*/
	virtual double getAudioClockTime() = 0;

	/**
	* @brief 设置音频硬件参数，供 getAudioClockTime() 计算使用
	*/
	virtual void setAudioHardwareParams(SDL_AudioDeviceID deviceId, int bytesPerSecond) = 0;

	/**
	* @brief 更新视频时钟的当前时间（单位：秒）
	* @param pts 视频帧的显示时间戳。
	*/
	virtual void setVideoClock(double pts) = 0;

	/**
	* @brief 获取视频时钟的当前时间（单位：秒）
	* @return 视频时钟的时间。
	*/
	virtual double getVideoClockTime() = 0;

	/**
	* @brief 获取外部时钟的当前时间（单位：秒）。
	* 通常基于播放开始后的系统时间流逝。
	* @return 外部时钟的时间
	*/
	virtual double getExternalClockTime() = 0;

	/**
	* @brief 暂停时钟。
	*/
	virtual void pause() = 0;

	/**
	* @brief 恢复时钟。
	*/
	virtual void resume() = 0;

	/**
	* @brief 查询时钟是否处于暂停状态。
	* @return 若时钟已暂停则返回 true，否则返回 false。
	*/
	virtual bool isPaused() const = 0;

	/**
	* @brief 重置时钟状态
	*/
	virtual void reset() = 0;

	/**
	 * @brief 将时钟设置为未同步状态 (NAN)。
	 * 用于直播流暂停恢复后，等待第一帧数据来校准时钟。
	 */
	virtual void setClockToUnknown() = 0;

	/**
	 * @brief 检查主时钟是否处于未同步状态。
	 */
	virtual bool isClockUnknown() = 0;

	/**
	 * @brief 强制将主时钟同步到指定的时间戳 (PTS)。
	 * 用于从 Unknown 状态恢复时，用第一帧的 PTS 校准系统时钟基准。
	 * @param pts 当前帧的时间戳 (秒)
	 */
	virtual void syncToPts(double pts) = 0;
};
