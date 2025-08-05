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

#include "IClockManager.h"//时钟管理器接口

//FFmpeg类型的前向声明
struct AVFrame;		// 数据帧结构体（包含解码后的视频数据）
enum AVPixelFormat;	// 像素格式枚举

class IVideoRenderer {
public:
	virtual ~IVideoRenderer() = default;

	/**
	* @brief 初始化视频渲染器。
	* 通常在此方法中创建窗口、渲染上下文（如SDL_Renderer）和纹理等。
	* @param windowTitle 窗口标题。
	* @param width 视频宽度（像素）。
	* @param height 视频高度（像素）。
	* @param decoderPixelFormat 解码器输出的原始视频帧的像素格式（例如 AV_PIX_FMT_YUV420P）。
	* 渲染器实现可能需要根据此格式处理数据，或进行转换。
	* @param clockManager 时钟管理器的指针，用于同步视频帧的显示。
	* @return 若初始化成功则返回 true，否则返回 false。
	*/
	virtual bool init(const char* windowTitle,
		int width,
		int height,
		enum AVPixelFormat decoderPixelFormat,//解码器输出的原始像素格式
		IClockManager* clockManager) = 0;

	/**
	* @brief 渲染（显示）一个视频帧。
	* 实现此方法时，需要包含音视频同步逻辑：
	* 1、获取视频帧的PTS（显示时间戳）。
	* 2、从IClockManager获取主时钟的当前时间。
	* 3、比较两者，以决定是立即显示、延迟显示还是（在更复杂的实现中）丢弃该帧。
	* 4、成功显示帧后，调用 IClockMangaer::setVideoClock() 更新视频时钟。
	* @param frame 指向包含解码后视频数据的 AVFrame 的指针。渲染器不拥有此帧。
	* 调用者在渲染完成后需要负责释放（例如通过 av_frame_unref)。
	* @return 若帧被成功处理（可能被显示或者按计划丢弃/延迟），则返回true。
	* 如果发生渲染错误，则返回false。
	*/
	virtual bool renderFrame(AVFrame* frame) = 0;

	/**
	* @brief 关闭视频渲染器并释放所有相关资源。
	* 例如销毁窗口、渲染器、纹理等。
	*/
	virtual void close() = 0;

	/**
	* @brief（可选）当窗口大小改变或者其它需要刷新UI状态时调用。
	* 对于最小可行播放器，主要通过renderFrame更新内容，该方法可能不急于实现复杂的逻辑。
	* 但可用于（例如在暂停时、或窗口事件后）触发一次屏幕刷新。
	*/
	virtual void refresh() = 0;

	// 播放/暂停功能主要由 MediaPlayer 控制 IClockManager 来实现。
	// IVideoRenderer 的 renderFrame 方法会根据 IClockManager 的状态（是否暂停，当前时间）
	// 来决定如何处理帧。因此，IVideoRenderer 接口本身不需要显式的 play/pause 方法
	// 来控制渲染逻辑，因为其行为是时钟驱动的。
	
	// 若需要 特定的渲染器行为（例如暂停时显式特定图像），则可以添加。
	// 对于最小可行播放器，此处假设 renderFrame 的同步逻辑会隐式处理暂停。

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
	* @brief 请求刷新
	*/
	virtual void requestRefresh() = 0;
};
