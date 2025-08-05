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

#include "IClockManager.h"//ʱ�ӹ������ӿ�

//FFmpeg���͵�ǰ������
struct AVFrame;		// ����֡�ṹ�壨������������Ƶ���ݣ�
enum AVPixelFormat;	// ���ظ�ʽö��

class IVideoRenderer {
public:
	virtual ~IVideoRenderer() = default;

	/**
	* @brief ��ʼ����Ƶ��Ⱦ����
	* ͨ���ڴ˷����д������ڡ���Ⱦ�����ģ���SDL_Renderer��������ȡ�
	* @param windowTitle ���ڱ��⡣
	* @param width ��Ƶ��ȣ����أ���
	* @param height ��Ƶ�߶ȣ����أ���
	* @param decoderPixelFormat �����������ԭʼ��Ƶ֡�����ظ�ʽ������ AV_PIX_FMT_YUV420P����
	* ��Ⱦ��ʵ�ֿ�����Ҫ���ݴ˸�ʽ�������ݣ������ת����
	* @param clockManager ʱ�ӹ�������ָ�룬����ͬ����Ƶ֡����ʾ��
	* @return ����ʼ���ɹ��򷵻� true�����򷵻� false��
	*/
	virtual bool init(const char* windowTitle,
		int width,
		int height,
		enum AVPixelFormat decoderPixelFormat,//�����������ԭʼ���ظ�ʽ
		IClockManager* clockManager) = 0;

	/**
	* @brief ��Ⱦ����ʾ��һ����Ƶ֡��
	* ʵ�ִ˷���ʱ����Ҫ��������Ƶͬ���߼���
	* 1����ȡ��Ƶ֡��PTS����ʾʱ�������
	* 2����IClockManager��ȡ��ʱ�ӵĵ�ǰʱ�䡣
	* 3���Ƚ����ߣ��Ծ�����������ʾ���ӳ���ʾ���ǣ��ڸ����ӵ�ʵ���У�������֡��
	* 4���ɹ���ʾ֡�󣬵��� IClockMangaer::setVideoClock() ������Ƶʱ�ӡ�
	* @param frame ָ������������Ƶ���ݵ� AVFrame ��ָ�롣��Ⱦ����ӵ�д�֡��
	* ����������Ⱦ��ɺ���Ҫ�����ͷţ�����ͨ�� av_frame_unref)��
	* @return ��֡���ɹ��������ܱ���ʾ���߰��ƻ�����/�ӳ٣����򷵻�true��
	* ���������Ⱦ�����򷵻�false��
	*/
	virtual bool renderFrame(AVFrame* frame) = 0;

	/**
	* @brief �ر���Ƶ��Ⱦ�����ͷ����������Դ��
	* �������ٴ��ڡ���Ⱦ��������ȡ�
	*/
	virtual void close() = 0;

	/**
	* @brief����ѡ�������ڴ�С�ı����������Ҫˢ��UI״̬ʱ���á�
	* ������С���в���������Ҫͨ��renderFrame�������ݣ��÷������ܲ�����ʵ�ָ��ӵ��߼���
	* �������ڣ���������ͣʱ���򴰿��¼��󣩴���һ����Ļˢ�¡�
	*/
	virtual void refresh() = 0;

	// ����/��ͣ������Ҫ�� MediaPlayer ���� IClockManager ��ʵ�֡�
	// IVideoRenderer �� renderFrame ��������� IClockManager ��״̬���Ƿ���ͣ����ǰʱ�䣩
	// ��������δ���֡����ˣ�IVideoRenderer �ӿڱ�����Ҫ��ʽ�� play/pause ����
	// ��������Ⱦ�߼�����Ϊ����Ϊ��ʱ�������ġ�
	
	// ����Ҫ �ض�����Ⱦ����Ϊ��������ͣʱ��ʽ�ض�ͼ�񣩣��������ӡ�
	// ������С���в��������˴����� renderFrame ��ͬ���߼�����ʽ������ͣ��

	/**
	 * @brief �����ڴ�С����
	 * @param newWidth �µĴ��ڿ��
	 * @param newHeight �µĴ��ڸ߶�
	 * @return �ɹ�����true��ʧ�ܷ���false
	 */
	virtual bool onWindowResize(int newWidth, int newHeight) = 0;

	/**
	 * @brief ��ȡ��ǰ���ڴ�С
	 * @param width ������������ڿ��
	 * @param height ������������ڸ߶�
	 */
	virtual void getWindowSize(int& width, int& height) const = 0;

	/**
	* @brief ����ˢ��
	*/
	virtual void requestRefresh() = 0;
};
