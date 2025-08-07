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
	 * @brief �����̳߳�ʼ����Ⱦ�����������ں����б�Ҫ��ͼ����Դ��
	 * @param windowTitle ���ڱ��⡣
	 * @param width ���ڿ�ȡ�
	 * @param height ���ڸ߶ȡ�
	 * @param decoderPixelFormat ��������������ظ�ʽ��
	 * @param clockManager ʱ�ӹ�����ʵ����
	 * @return �ɹ����� true��ʧ�ܷ��� false��
	 */
	virtual bool init(const char* windowTitle, int width, int height,
		enum AVPixelFormat decoderPixelFormat, IClockManager* clockManager) = 0;

	/**
	 * @brief ������Ƶ֡Ӧ�õȴ���ͬ���ӳ�ʱ�䣨����Ϊ��λ����
	 *
	 * ����Ƶͬ���߼��ĺ��ġ��Ƚϴ���֡����ʾʱ�����PTS������ʱ�ӣ�ͨ������Ƶʱ�ӣ��ĵ�ǰʱ�䡣
	 * �ü���������ָ�������ߣ�ͨ������Ƶͬ���̣߳�Ӧ���ӳٶ����������Ⱦ����ȷ������������ͬ����
	 * ��Ӧ����׼��֡���ݣ�prepareFrameForDisplay��֮ǰ�����á�
	 *
	 * @note �˺������̰߳�ȫ�ģ���������ڡ��������̡߳�������Ƶͬ���̣߳��е��á�
	 *
	 * @param frame ָ���������Ƶ���� AVFrame ��ָ�롣������Ӵ�֡����ȡPTS��
	 * @return double ���͵��ӳ�ʱ�䣬��λΪ�롣
	 * - �������ֵ > 0����ʾ��Ƶ������ʱ�ӣ�������Ӧ�ӳ���Ӧ��ʱ�䡣
	 * - �������ֵ <= 0����ʾ��Ƶ���ڻ�������ʱ�ӣ�������Ӧ����������Ⱦ������ȴ���
	*/
	virtual double calculateSyncDelay(AVFrame* frame) = 0;

	/**
	 * @brief ׼��һ������������ʾ����Ƶ֡��ִ�����з���Ⱦ��Ԥ��������
	 *
	 * �˺�������ִ��CPU�ܼ��͵�׼���������罫��Ƶ֡�ӽ����������ظ�ʽ���� YUV420P��
	 * ת��Ϊ��Ⱦ��������м��ʽ���� I420���������Ὣ׼���õ�֡���ݻ���������
	 * �Ա���� displayFrame() �� refresh() ���Կ��ٷ��ʡ�
	 *
	 * @note �˺������̰߳�ȫ�ģ���������ڡ��������̡߳�������Ƶͬ���̣߳��е��ã�
	 * �Ա�����������Ⱦ�̡߳�
	 *
	 * @param frame ָ�������� AVFrame ��ָ�롣�����ڲ����ܻ����ô�֡�����磬���䱣��Ϊ�����һ֡������
	 * �������ں������غ���Ȼ��Ҫ���� av_frame_unref()��
	 * @return ���֡���ݳɹ�׼�������棬�򷵻� true��
	 * �������������ת��ʧ�ܣ����򷵻� false��
	 */
	virtual bool prepareFrameForDisplay(AVFrame* frame) = 0;
	
	/**
	 * @brief �����һ��׼���õ���Ƶ֡ʵ����Ⱦ����Ļ�ϡ�
	 *
	 * �˺���ִ��������ͼ��API����SDL, OpenGL, D3D����صĲ�����������������
	 * �����Ⱦ��������������ȾĿ�겢���ճ��ֻ��档��Ӧ��ʹ���� prepareFrameForDisplay()
	 * ׼���ͻ�������ݡ�
	 *
	 * @warning �˺��������ڡ����߳�/UI�̡߳��е��ã�����ѭͼ�ο���߳��׺��Թ���
	 * ���̵߳��ÿ��ܵ�����Ⱦʧ�ܡ����������������
	 *
	 * @note �˺��������ܲ�������Ϊ����Ⱦ�����ڲ������֡��
	 */
	virtual void displayFrame() = 0; // �����߳��е���

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
};
