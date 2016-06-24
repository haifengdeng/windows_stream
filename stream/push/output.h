#ifndef FFMPEG_OUTPUT_HEADER_H
#define FFMPEG_OUTPUT_HEADER_H

#include "config.h"

extern "C"{
	struct videoCodec_cfg
	{
		enum AVCodecID codecId;
		int width;
		int height;
		enum AVPixelFormat pix_fmt;
		AVRational frame_rate;
		AVRational timebase;
		AVRational frame_aspect_ratio;
		AVDictionary *codec_opts;
		int video_bitrate;
	};
	typedef struct videoCodec_cfg videoCodec_cfg_t;

	struct audioCodec_cfg{
		enum AVCodecID codecId;
		int channels;
		enum AVSampleFormat sample_fmt;
		int sample_rate;
		AVRational timebase;
		AVDictionary *codec_opts;
		int audio_bitrate;
	};

	typedef struct audioCodec_cfg audioCodec_cfg_t;

	struct ffmpeg_cfg {
		const char         *url;
		const char         *format_name;
		const char         *format_mime_type;
		const char         *muxer_settings;
#if 0
		const char         *video_encoder;
		AVCodecID          video_encoder_id;
		const char         *audio_encoder;
		AVCodecID          audio_encoder_id;
		const char         *video_settings;
		const char         *audio_settings;
		enum AVPixelFormat format;
		enum AVColorRange  color_range;
		enum AVColorSpace  color_space;
		int                scale_width;
		int                scale_height;
		int                width;
		int                height;
#endif
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
	ffmpeg_data();
	virtual ~ffmpeg_data(){
		if (mInitialized)
		{
			free();
		}
	}

	bool ffmpeg_data_init(ffmpeg_cfg *cfg);
	bool ffmpeg_data_close(){
		free();
		return true;
	}
	int writePacket(AVPacket*packet);
	int encode_video(AVPacket *packet, AVFrame *frame, int *got_packet);
	int encode_audio(AVPacket *packet, AVFrame *frame, int *got_packet);

	friend ffmpeg_output;
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
	void free();
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

	int64_t            mTotal_frames;
	AVFrame            *mVframe;


	int64_t            mTotal_samples;
	AVFrame            *mAframe;
	int                mAFrame_size;
	SwrContext         *mAudioSwrCtx;

	struct ffmpeg_cfg  mConfig;

	bool               mInitialized;
private:
	void reset_ffmpeg_data();
};

class ffmpeg_output {
public:
	ffmpeg_output();
	virtual ~ffmpeg_output(){}

	bool start_output(struct ffmpeg_cfg*cfg);
	bool close_output();
	int audio_frame(AVFrame *frame);
	int video_frame(AVFrame *frame);
	int get_audio_frame_size(){
		return mff_data.mAFrame_size;
	}

public:
	static int write_thread_func(ffmpeg_output *output);
	int write_thread();
private:
	volatile bool      mActive;
	ffmpeg_data mff_data;
	uint64_t           mStart_timestamp;

	bool               mConnecting;
	//boost::thread*          mStart_thread;

	bool               mWrite_thread_active;
	boost::mutex         mWrite_mutex;
	boost::thread        *mWrite_thread;

	boost::condition_variable  mWrite_cv;
    boost::mutex         mMutex_cv;

	bool               mStop_event;

	std::queue<AVPacket>   mPackets;
	bool mAbort;
private:
	int process_packet();
	void push_back_packet(AVPacket *pkt);
	void resetValue()
	{
		mActive = false;
		mStart_timestamp = 0;

		mConnecting = false;
		//boost::thread*          mStart_thread;

		mWrite_thread_active = false;
		mWrite_thread = false;

		mStop_event = mAbort=false;
	}
};
#endif