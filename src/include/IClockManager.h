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

// 音频作为主参考，外部时钟作为无音频时的备用，
// 该枚举可以指导 ClockManager 内部如何选择 getMasterClockTime 的源
// 对于最小可行播放器，该枚举甚至可以不用在接口暴露，而作为内部逻辑
enum class InitialMasterHint {
	PREFER_AUDIO,//若有音频，优先用音频
	PREFER_EXTERNAL//优先用外部时钟（如纯视频）
};

class IClockManager {
public:
	virtual ~IClockManager() = default;

	/**
	* @brief 初始化时钟管理器。
	* @param hint 提示初始化时倾向于使用哪种时钟作为参考，主要影响无音频情况。
	*/
	virtual void init(InitialMasterHint hint = InitialMasterHint::PREFER_AUDIO) = 0;

	/**
	* @brief 获取主时钟的当前时间（单位：秒）。
	* 实现类内部会根据是否有音频等情况决定返回哪个时钟值。
	* @return 主时钟的当前时间。
	*/
	virtual double getMasterClockTime() = 0;

	/**
	* @brief 更新音频时钟的当前时间（单位：秒）
	* @param pts 音频帧的显示时间戳。
	*/
	virtual void setAudioClock(double pts) = 0;

	/**
	* @brief 获取音频时钟的当前时间（单位：秒）
	* @return 音频时钟的时间。
	*/
	virtual double getAudioClockTime() = 0;

	/**
	* @brief 设置音频硬件参数，供getAudioClockTime()计算使用
	*/
	virtual void setAudioHardwareParams(SDL_AudioDeviceID deviceId, int bytesPerSecond, bool hasAudioStream) = 0;

	/**
	* @brief 更新视频时钟的当前时间（单位：秒）
	* @param pts 视频帧的显示时间戳。
	* @param duration 视频帧的持续时间（可选，可用于更平滑的视频时钟）。
	*/
	virtual void setVideoClock(double pts, double duration = 0.0) = 0;

	/**
	* @brief 获取视频时钟的当前时间（单位：秒）
	* @return 视频时钟的时间。
	*/
	virtual double getVideoClockTime() = 0;

	//外部时钟通常由系统时间驱动，setExternalClock 可能不直接设置一个pts，
	//而是校准一个起始点。getExternalClockTime 会返回（当前系统时间 - 播放开始的系统时间点）。
	//为了简化，可以让setExternalClock更像其它setters，但这取决于具体实现。
	//对于最小可行版本，可以简化为只在内部使用，无需set接口，get接口返回基于播放开始的流逝时间。
	//或者保留一个简单的set来同步起始点。

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
	* @brief 关闭并清理时钟管理器资源。
	*/
	virtual void close() = 0;
};
