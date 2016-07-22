#ifndef FFMPEG_OUTPUT_HEADER_H
#define FFMPEG_OUTPUT_HEADER_H

#include "config.h"
#include "boost_thread.h"
#include "stream_output.h"
extern "C"{
	struct videoCodec_cfg
	{
		enum AVCodecID codecId;
		int width;
		int height;
		enum AVPixelFormat pix_fmt;
		int  frame_rate;
		int video_bitrate;
	};
	typedef struct videoCodec_cfg videoCodec_cfg_t;

	struct audioCodec_cfg{
		enum AVCodecID codecId;
		int channels;
		enum AVSampleFormat sample_fmt;
		int sample_rate;
		int audio_bitrate;
	};

	typedef struct audioCodec_cfg audioCodec_cfg_t;

	struct ffmpeg_cfg {
		const char         *url;
		const char         *format_name;
		audioCodec_cfg_t audioconfig;
		videoCodec_cfg_t videoconfig;
	};
}

int astrcmpi_n(const char *str1, const char *str2, size_t n);
inline const char *safe_str(const char *s)
{
	if (s == NULL)
		return "(NULL)";
	else
		return s;
}

class ffmpeg_output;
class ffmpeg_data {
public:
	friend ffmpeg_output;
	ffmpeg_data(struct ffmpeg_cfg & cfg);
	virtual ~ffmpeg_data();

	bool start();
	void close();
	//
	int writePacket(AVPacket*packet);
	int encode_video(AVPacket *packet, AVFrame *frame, int *got_packet);
	int encode_audio(AVPacket *packet, AVFrame *frame, int *got_packet);

private:
	bool open_video_codec();
    bool open_audio_codec();
	bool sample_fmt_support(const AVCodec* codec, enum AVSampleFormat fmt);
	bool pixel_fmt_support(const AVCodec * codec, enum AVPixelFormat pix_fmt);
	bool create_video_stream();
	bool create_audio_stream();
	bool init_streams();
	bool open_output_file();
	//close
	void close_video();
	void close_audio();

private:
	AVStream           *mVideoStream;
	AVStream           *mAudioStream;
	AVCodec            *mAcodec;
	AVCodec            *mVcodec;
	AVCodecContext     *mAudioCodecCtx;
	AVCodecContext     *mVideoCodecCtx;
	AVFormatContext    *mOutput;

	struct SwsContext  *mSwscale;
	AVFrame            *mVframe;


	AVFrame            *mAframe;
	SwrContext         *mAudioSwrCtx;

	struct ffmpeg_cfg  mConfig;

	bool               mInitialized;
	int64_t mStartPTS;
};

class ffmpeg_output:
	public stream_output
{
public:
	ffmpeg_output(Json::Value &settings);
	virtual ~ffmpeg_output();

	//virtual 
	virtual std::string getOutputId();
	virtual int getOutputflags();
	virtual bool start();
	virtual void stop();
	virtual bool isactived();
	virtual void raw_video(AVFrame *frames);
	virtual void raw_audio(AVFrame *frames);
	virtual void encoded_packet(AVPacket *pakcet);
	virtual void update(Json::Value &settings);
	virtual void get_properties(Json::Value &props);

public:
	static int write_thread_func(void *param);
	static const int maxPacketsArraySize =30;
	int write_thread();

private:
	bool push_back_packet(AVPacket *pkt, bool audio);
	int process_packet();

private:
	volatile bool      mActive;
	ffmpeg_data*       mff_data;
	ffmpeg_cfg *       mConfig;


	boost_thread        *mWrite_thread;
	std::condition_variable  mWrite_cv;
	std::mutex         mMutex_cv;

	std::queue<AVPacket>   mAudioPackets;
	std::queue<AVPacket>   mVideoPackets;
	std::mutex         mWrite_mutex;

	bool mAbort;
};
#endif