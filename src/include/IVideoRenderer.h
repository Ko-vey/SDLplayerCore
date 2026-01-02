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

#include <memory>

#include "IClockManager.h"
#include "PlayerDebugStats.h"

struct AVFrame;		// 数据帧结构体（包含解码后的视频数据）
enum AVPixelFormat;	// 像素格式枚举

// 默认帧持续时间（秒），对应25fps，用于无duration信息的帧
constexpr double DEFAULT_FRAME_DURATION = 0.04;

// 同步阈值：视频落后超过此值，则不进行延迟，加速追赶
constexpr double AV_SYNC_THRESHOLD_MIN = 0.04;

// 同步阈值：视频落后超过此值，则触发丢帧决策
constexpr double AV_SYNC_THRESHOLD_MAX = 0.1;

// 同步信号：由 calculateSyncDelay 返回，请求调用者丢弃当前帧
constexpr double SYNC_SIGNAL_DROP_FRAME = -1.0;

class IVideoRenderer {
public:
	virtual ~IVideoRenderer() = default;

	/**
	 * @brief 在主线程初始化渲染器，创建窗口和所有必要的图形资源。
	 * @param windowTitle 窗口标题。
	 * @param width 窗口宽度。
	 * @param height 窗口高度。
	 * @param decoderPixelFormat 解码器输出的像素格式。
	 * @param clockManager 时钟管理器实例。
	 * @return 成功返回 true，失败返回 false。
	 */
	virtual bool init(const char* windowTitle, int width, int height,
		enum AVPixelFormat decoderPixelFormat, IClockManager* clockManager) = 0;

	/**
	 * @brief 计算视频帧应该等待的同步延迟时间（以秒为单位）。
	 *
	 * 音视频同步逻辑的核心。比较传入帧的显示时间戳（PTS）和主时钟（通常是音频时钟）的当前时间。
	 * 该计算结果用于指导调用者（通常是视频同步线程）应该延迟多久再请求渲染，以确保画面与声音同步。
	 * 它应该在准备帧数据（prepareFrameForDisplay）之前被调用。
	 *
	 * @note 此函数是线程安全的，设计用于在【工作者线程】（如视频同步线程）中调用。
	 *
	 * @param frame 指向解码后的视频数据 AVFrame 的指针。函数会从此帧中提取PTS。
	 * @return double 类型的延迟时间，单位为秒。
	 *   > 0.0: 视频早于主时钟，调用者需要延迟的时间（秒）。
	 *   = 0.0: 应立即显示
	 *   = SYNC_SIGNAL_DROP_FRAME: 视频严重滞后，应丢弃此帧
	*/
	virtual double calculateSyncDelay(AVFrame* frame) = 0;

	/**
	 * @brief 准备一个用于最终显示的视频帧，执行所有非渲染的预处理工作。
	 *
	 * 此函数负责执行CPU密集型的准备任务，例如将视频帧从解码器的像素格式（如 YUV420P）
	 * 转换为渲染器所需的中间格式（如 I420）。它还会将准备好的帧数据缓存起来，
	 * 以便后续 displayFrame() 或 refresh() 可以快速访问。
	 *
	 * @note 此函数是线程安全的，设计用于在【工作者线程】（如视频同步线程）中调用，
	 * 以避免阻塞主渲染线程。
	 *
	 * @param frame 指向待处理的 AVFrame 的指针。函数内部可能会引用此帧（例如，将其保存为“最后一帧”）。
	 * 调用者在函数返回后仍然需要负责 av_frame_unref()。
	 * @return 如果帧数据成功准备并缓存，则返回 true。
	 * 如果发生错误（如转换失败），则返回 false。
	 */
	virtual bool prepareFrameForDisplay(AVFrame* frame) = 0;
	
	/**
	 * @brief 将最近一次准备好的视频帧实际渲染到屏幕上。
	 *
	 * 此函数执行所有与图形API（如SDL, D3D）相关的操作，包括更新纹理、
	 * 清空渲染器、拷贝纹理到渲染目标并最终呈现画面。它应该使用由 prepareFrameForDisplay()
	 * 准备和缓存的数据。
	 *
	 * @warning 此函数必须在【主线程/UI线程】中调用，以遵循图形库的线程亲和性规则。
	 * 跨线程调用可能导致渲染失败、程序锁死或崩溃。
	 *
	 * @note 此函数不接受参数，因为它渲染的是内部缓存的帧。
	 */
	virtual void displayFrame() = 0; // 在主线程中调用

	/**
	* @brief 关闭视频渲染器并释放所有相关资源。
	* 例如销毁窗口、渲染器、纹理等。
	*/
	virtual void close() = 0;

	/**
	* @brief 当窗口大小改变或者其它需要刷新UI状态时调用。
	* 可用于（例如在暂停时、或窗口事件后）触发一次屏幕刷新。
	*/
	virtual void refresh() = 0;

	/**
	 * @brief 处理窗口大小调整
	 * @param newWidth 新的窗口宽度
	 * @param newHeight 新的窗口高度
	 * @return 成功返回true，失败返回false
	 */
	virtual bool onWindowResize(int newWidth, int newHeight) = 0;

	/**
	 * @brief 获取当前窗口大小
	 * @param width 输出参数：窗口宽度
	 * @param height 输出参数：窗口高度
	 */
	virtual void getWindowSize(int& width, int& height) const = 0;

	/**
	 * @brief 获取当前播放器全局的调试信息
	 * @param stats 当前状态信息
	 */
	virtual void setDebugStats(std::shared_ptr<PlayerDebugStats> stats) = 0;
};
