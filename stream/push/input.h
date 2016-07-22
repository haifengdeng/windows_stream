#ifndef FFMPEG_INPUT_HEADER_H
#define FFMPEG_INPUT_HEADER_H

#include "config.h"
#include "boost_thread.h"
#include "stream_source.h"
class source_callback{
public:
	virtual void audio_callback(AVFrame * frame) = 0;
	virtual void video_callback(AVFrame * frame) = 0;
};


struct input_config
{
	//global
	char *input;
	char *input_format;
	//video
	enum AVPixelFormat pix_fmt;
	int width;
	int height;
	int Fps;
	//audio
	int channel;
	int bitsPerSample;
	int frequency;
};

class  ff_decoder {
public:
	ff_decoder(AVCodecContext *codec_context,AVStream *stream,source_callback *callback);
	virtual ~ff_decoder();
	void decoder_abort();
	void pushback_packet(AVPacket* pkt);
	bool ff_decoder_start();
	static int decoder_thread(void *param);
	static const int maxPacketQueueSize = 10;
private:
	int decode_frame(AVFrame *frame, bool *frame_complete);
    int audio_decoder_thread();
    int video_decoder_thread();

private:
	AVCodecContext *mCodec;
	AVStream *mStream;
	source_callback *mCallback;

	boost_thread* mDecoder_thread;
	std::queue<AVPacket> mPacketQueue;
	bool mAbort;

	bool mPacket_pending;

	AVPacket mPkt_temp;
	AVPacket mPkt;
	std::condition_variable  mWrite_cv;
	std::mutex         mMutex_cv;
	std::mutex   mPacketMutex;
};

class ff_demuxer
{
public:
	ff_demuxer(input_config *config, source_callback * callback);
	virtual ~ff_demuxer();

	int startDemuxer();
	void stopDemuxer();
	bool isAborted();
	//thread
	static int demuxer_thread(void *param);
	int demuxer_loop();
private:
	int open_input();	
	int stream_component_open(int streamIndex);
	void stream_component_close(int streamIndex);
	int find_and_initialize_stream_decoders();
	void pushEmptyPacket();
	void setFmtOpts(AVDictionary ** fmt_opts);

private:
	input_config * mConfig;
	AVIOContext *mIoContext;
	AVFormatContext *mFmtContext;
	AVInputFormat *mInputFormat;

	ff_decoder *mAudio_decoder;
	ff_decoder *mVideo_decoder;

	boost_thread *mDemuxer_thread;
	source_callback *mCallback;

	AVRational mAudioStream_tb;
	AVRational mVideoStream_tb;

	int mAudioIndex;
	int mVideoIndex;
	int64_t mStartPTS;
	bool mAbort;
};

class ffmpegSource_callback;
class ffmpeg_source :public stream_source
{
public:
	static void getSourceDefaultSetting(Json::Value &setting);
public:
	ffmpeg_source(Json::Value & info);
	~ffmpeg_source();
	//stream source interface
	virtual void getSourceStringId(std::string & id);
	virtual int getSourceType();
	virtual int getSourceFlag();
	virtual int activeSourceStream();
	virtual int deactiveSourceStream();
	virtual bool isactive();
	virtual int updateSourceSetting(Json::Value & info);
	virtual void getSourceProperties(Json::Value &props, propertytype flag);
	virtual bool getSourceFrame(AVFrame* frame, bool audio);
	virtual AVFrame* getSourceFrame(bool audio);

private:
	friend ffmpegSource_callback;
	void audio_callback(AVFrame * frame);
	void video_callback(AVFrame * frame);
private:
	ff_demuxer    *mDemuxer;
	input_config  mInputCfg;
	ffmpegSource_callback *mCallback;
    std::queue<AVFrame*> mAudioFrameQueue;
	std::queue<AVFrame*> mVideoFrameQueue;
	std::mutex mMutex;

	//input
	bool mActived;
};

#endif