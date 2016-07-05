#ifndef FFMPEG_INPUT_HEADER_H
#define FFMPEG_INPUT_HEADER_H

#include "config.h"
#include "boost_thread.h"
class decoder_callback{
public:
	virtual void audio_callback(AVStream *stream, AVCodecContext* codec_ctx,AVFrame * frame) = 0;
	virtual void video_callback(AVStream *stream, AVCodecContext* codec_ctx,AVFrame * frame) = 0;
	virtual void demuxer_ready_callback()=0;
};

class  ff_decoder {
public:
	ff_decoder(AVCodecContext *codec_context,AVStream *stream,
		unsigned int packet_queue_size);
	virtual ~ff_decoder(){}
	void decoder_abort();
	void push_back_packet(AVPacket* pkt);
	bool ff_decoder_start();
	static int decoder_thread(void *param);
private:
	int decode_frame(AVFrame *frame, bool *frame_complete);
    int audio_decoder_thread();
    int video_decoder_thread();

public:
	AVCodecContext *mCodec;
	AVStream *mStream;

	boost_thread* mDecoder_thread;
	decoder_callback *mCallback;

	std::queue<AVPacket> mPacket_queue;
	std::queue<AVFrame*> mFrame_queue;
	unsigned int mPacket_queue_size;

	double mTimer_next_wake;
	double mPrevious_pts;       // previous decoded frame's pts
	double mPrevious_pts_diff;  // previous decoded frame pts delay
	double mPredicted_pts;      // predicted pts of next frame
	double mCurrent_pts;        // pts of the most recently dispatched frame
	int64_t mCurrent_pts_time;  // clock time when current_pts was set
	int64_t mStart_pts;

	bool mHwaccel_decoder;
	enum AVDiscard mFrame_drop;

	bool mFirst_frame;
	bool mEof;
	bool mAbort;

	bool mFinished;
	bool mPacket_pending;
	AVPacket mPkt_temp;
	AVPacket mPkt;
	boost::condition_variable  mWrite_cv;
	boost::mutex         mMutex_cv;
	void resetValue()
	{
		mCodec = NULL;
		mStream = NULL;

		mDecoder_thread = NULL;
		mCallback = NULL;

		mPacket_queue_size = 0;

		mTimer_next_wake=0;
		mPrevious_pts=0;
		mPrevious_pts_diff=0;
		mPredicted_pts=0;      
		mCurrent_pts=0;       
		mCurrent_pts_time=0; 
		mStart_pts=0;

		mHwaccel_decoder = 0;
		mFrame_drop = AVDISCARD_NONE;

		mFirst_frame = mEof = mAbort = false;

		mFinished = false;
		mPacket_pending = false;
		av_init_packet(&mPkt_temp);
		av_init_packet(&mPkt);
	}
};

class ff_demuxer {
public:
	ff_demuxer(){ resetValue(); }
	virtual ~ff_demuxer(){}

	int demuxer_open(const char *filename, char *input_format,decoder_callback *callback);
	void demuxer_close();
	static int demux_thread(void *param);
public:
	decoder_callback *mCallback;
public:
	AVIOContext *mIo_context;
	AVFormatContext *mFormat_context;
	AVInputFormat *mFile_iformat;
	int mAudio_st_index;
	int mVideo_st_index;

	ff_decoder *mAudio_decoder;
	ff_decoder *mVideo_decoder;

	boost_thread *mDemuxer_thread;

	int64_t mStart_pos;

	int64_t mSeek_pos;
	bool mSeek_request;
	int mSek_flags;
	bool mSeek_flush;

	bool mAbort;

	bool mRealtime;
	bool mVideo_disable;
	bool mAudio_disable;
	bool mEof;

	char *mInput;
	char *mInput_format;
private:
	int demux_loop();
	int open_input();	
	int stream_component_open(int stream_index);
	void stream_component_close(int stream_index);
	int find_and_initialize_stream_decoders();
	int put_nullpacket(int stream_index);
public:
	//input video para
	int mWidth;
	int mHeight;
	enum AVPixelFormat mpix_fmt;
	AVRational mframe_rate;
	AVRational mVtimebase;//AVCodecContext::timebase
	AVRational mframe_aspect_ratio;

	//input audio para
	int mfreq;
	int mchannels;
	int64_t mchannel_layout;
	enum AVSampleFormat mfmt;
	AVRational mAtimebase;//AVCodecContext::timebase


	AVRational mAudioStream_tb;
	AVRational mVideoStream_tb;

	void resetValue()
	{
		mCallback = NULL;
		mIo_context = NULL;
		mFormat_context = NULL;
		mFile_iformat = NULL;
		mAudio_st_index = mVideo_st_index = -1;
		mAudio_decoder = mVideo_decoder = NULL;
		mDemuxer_thread = NULL;
		mStart_pos = mSeek_pos = AV_NOPTS_VALUE;
		mSeek_request = mSeek_flush = false;
		mSek_flags = 0;
		mAbort = false;
		mRealtime = false;
		mVideo_disable = false;
		mAudio_disable = false;
		mEof = false;
		mInput = mInput_format = NULL;
		mWidth = mHeight = 0;
	    mpix_fmt = AV_PIX_FMT_NONE;
		mframe_rate = {0 ,0 };
		mVtimebase = std_tb_us;
		mframe_aspect_ratio = {0,0 };

		//input audio para
		mfreq = 0;
		mchannels = 0;
		mchannel_layout = 0;
		mfmt = AV_SAMPLE_FMT_NONE;
		mAtimebase = std_tb_us;
		mVideoStream_tb = std_tb_us;
		mAudioStream_tb = std_tb_us;
	}
};



#endif