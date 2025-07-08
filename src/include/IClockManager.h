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

// ��Ƶ��Ϊ���ο����ⲿʱ����Ϊ����Ƶʱ�ı��ã�
// ��ö�ٿ���ָ�� ClockManager �ڲ����ѡ�� getMasterClockTime ��Դ
// ������С���в���������ö���������Բ����ڽӿڱ�¶������Ϊ�ڲ��߼�
enum class InitialMasterHint {
	PREFER_AUDIO,//������Ƶ����������Ƶ
	PREFER_EXTERNAL//�������ⲿʱ�ӣ��紿��Ƶ��
};

class IClockManager {
public:
	virtual ~IClockManager() = default;

	/**
	* @brief ��ʼ��ʱ�ӹ�������
	* @param hint ��ʾ��ʼ��ʱ������ʹ������ʱ����Ϊ�ο�����ҪӰ������Ƶ�����
	*/
	virtual void init(InitialMasterHint hint = InitialMasterHint::PREFER_AUDIO) = 0;

	/**
	* @brief ��ȡ��ʱ�ӵĵ�ǰʱ�䣨��λ���룩��
	* ʵ�����ڲ�������Ƿ�����Ƶ��������������ĸ�ʱ��ֵ��
	* @return ��ʱ�ӵĵ�ǰʱ�䡣
	*/
	virtual double getMasterClockTime() = 0;

	/**
	* @brief ������Ƶʱ�ӵĵ�ǰʱ�䣨��λ���룩
	* @param pts ��Ƶ֡����ʾʱ�����
	*/
	virtual void setAudioClock(double pts) = 0;

	/**
	* @brief ��ȡ��Ƶʱ�ӵĵ�ǰʱ�䣨��λ���룩
	* @return ��Ƶʱ�ӵ�ʱ�䡣
	*/
	virtual double getAudioClockTime() = 0;

	/**
	* @brief ������ƵӲ����������getAudioClockTime()����ʹ��
	*/
	virtual void setAudioHardwareParams(SDL_AudioDeviceID deviceId, int bytesPerSecond, bool hasAudioStream) = 0;

	/**
	* @brief ������Ƶʱ�ӵĵ�ǰʱ�䣨��λ���룩
	* @param pts ��Ƶ֡����ʾʱ�����
	* @param duration ��Ƶ֡�ĳ���ʱ�䣨��ѡ�������ڸ�ƽ������Ƶʱ�ӣ���
	*/
	virtual void setVideoClock(double pts, double duration = 0.0) = 0;

	/**
	* @brief ��ȡ��Ƶʱ�ӵĵ�ǰʱ�䣨��λ���룩
	* @return ��Ƶʱ�ӵ�ʱ�䡣
	*/
	virtual double getVideoClockTime() = 0;

	//�ⲿʱ��ͨ����ϵͳʱ��������setExternalClock ���ܲ�ֱ������һ��pts��
	//����У׼һ����ʼ�㡣getExternalClockTime �᷵�أ���ǰϵͳʱ�� - ���ſ�ʼ��ϵͳʱ��㣩��
	//Ϊ�˼򻯣�������setExternalClock��������setters������ȡ���ھ���ʵ�֡�
	//������С���а汾�����Լ�Ϊֻ���ڲ�ʹ�ã�����set�ӿڣ�get�ӿڷ��ػ��ڲ��ſ�ʼ������ʱ�䡣
	//���߱���һ���򵥵�set��ͬ����ʼ�㡣

	/**
	* @brief ��ȡ�ⲿʱ�ӵĵ�ǰʱ�䣨��λ���룩��
	* ͨ�����ڲ��ſ�ʼ���ϵͳʱ�����š�
	* @return �ⲿʱ�ӵ�ʱ��
	*/
	virtual double getExternalClockTime() = 0;

	/**
	* @brief ��ͣʱ�ӡ�
	*/
	virtual void pause() = 0;

	/**
	* @brief �ָ�ʱ�ӡ�
	*/
	virtual void resume() = 0;

	/**
	* @brief ��ѯʱ���Ƿ�����ͣ״̬��
	* @return ��ʱ������ͣ�򷵻� true�����򷵻� false��
	*/
	virtual bool isPaused() const = 0;

	/**
	* @brief ����ʱ��״̬
	*/
	virtual void reset() = 0;

	/**
	* @brief �رղ�����ʱ�ӹ�������Դ��
	*/
	virtual void close() = 0;
};
