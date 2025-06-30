//IDemuxer�ӿڣ����� ���װ��Demuxer�Ļ������ܵĳ�����Լ
#pragma once

// FFmpeg���͵�ǰ���������Ա����ڴ˴����� �Ӵ��ͷ�ļ�
struct AVFormatContext;
struct AVPacket;
struct AVCodecParameters;
struct AVRational;
enum AVMediaType;//ֱ��ʹ��enum

class IDemuxer {
public:
	// �������������ڽӿ�*�ǳ��ؼ�*����ȷ�� ���ͨ������ָ�� ɾ������ʱ ��Դ�ܱ���ȷ����
	virtual ~IDemuxer() = default;//=default ���߱�����Ϊ�����������������һ��Ĭ�ϵı�׼ʵ��

	//
	//����ֵ��
	/**
	 * @brief ��ý��Դ���ļ�·������URL��
	 * @param 
	 * @return �ɹ�����true��ʧ�ܷ���false
	 */
	virtual bool open(const char* url) = 0;

	/**
	 * @brief �ر�ý��Դ���ͷ���Դ
	 */
	virtual void close() = 0;

	/**
	 * @brief ��ȡ��һ�����ݰ�
	 * @param packet �������ṩ�� AVPacket �ṹ��ָ�룬���ڽ�������
	 * @return �ɹ�����0���ļ���������AVERROR_EOF�����������򷵻�<0������
	 */
	virtual int readPacket(AVPacket* packet) = 0;

	/**
	 * @brief ��ȡ�ײ�� AVFormatContext���������ڻ�ȡ����ϸ��Ϣ��
	 * ע�⣺ͨ��Ӧ������¶�ײ������ģ�����ʱ�б�Ҫ������FFmpeg�������
	 * Ϊ�˸��õĳ��󣬿����Ƴ���һ�ӿ�
	 * @return ָ�� AVFormatContext ��ָ�롣��δ����Ϊ nullptr
	 */
	virtual AVFormatContext* getFormatContext() const = 0;// const ��ʾ�ú��������޸����ڳ�Ա���������Ǳ�����mutable�ģ�

	/**
	 * @brief ����ָ��ý�����͵�������
	 * @param type AVMEDIA_TYPE_VIDEO, AVMEDIA_TYPE_AUDIO, etc.
	 * @return ��������>=0��,��δ�ҵ��򷵻� -1
	 */
	virtual int findStream(AVMediaType type) const = 0;

	/**
	 * @brief ��ȡָ�����ı����������
	 * @param streamIndex ��������
	 * @return ָ�� AVCodecParameters ��ָ�룬��������Ч��Ϊ nullptr
	 */
	virtual AVCodecParameters* getCodecParameters(int streamIndex)const = 0;

	/**
	 * @brief ��ȡʱ��
	 */
	virtual double getDuration() const = 0;

	/**
	 * @brief ��ȡָ������ʱ��� (time_base).
	 * @param streamIndex ��������.
	 * @return AVRational �ṹ�壬��ʾʱ�������������Ч�򷵻� {0, 1}��
	 */
	virtual AVRational getTimeBase(int streamIndex) const = 0;
	
	//������ͨ�ý��װ���Ĺ��ܣ����ȡԪ���ݵȣ�
	//virtual AVDictionary* getMetadata() const = 0;
};
