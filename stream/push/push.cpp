#include "input.h"
#include "output.h"
#include "filter.h"
int init_ffmpeg()
{
#if CONFIG_AVDEVICE
	avdevice_register_all();
#endif
#if CONFIG_AVFILTER
	avfilter_register_all();
#endif
	av_register_all();
	avformat_network_init();
	return 0;
}

class push_callback;
class push{
public:
	struct push_config{
	public:
		char *input;
		char *input_format;
		
		char *output;
		char *output_format;
		char *video_codec;
		char *audio_codec;
		push_config(){ memset(this, 0, sizeof(struct push_config)); }
	};
public:
	push(struct push_config *config);
	virtual ~push(){}
	int start_push();
	int stop_push();
	static int video_thread(push * _push);
	static int audio_thread(push * _push);
	void audio_callback(AVStream *stream, AVCodecContext* codec_ctx, AVFrame * frame);
	void video_callback(AVStream *stream, AVCodecContext* codec_ctx, AVFrame * frame);
	void demuxer_ready_callback();
	int video_push_thread();
	int audio_push_thread();
private:
	av_sync        mSync;
	ff_demuxer    *mDemuxer;
	ffmpeg_output *mOutput;
	audiofilter   *mAfilter;
	videofilter   *mVfilter;
	push_callback *mCallback;

	boost::thread *mVideoThread;
	boost::thread *mAudioThread;
	bool mAbort;
	
	bool mInited;
	ffmpeg_cfg mOutcfg;
	push_config  mConfig;

	//for record
	uint64_t mVtotalFrames;
	uint64_t mVlaggedFrames;

	uint64_t mAtotalFrames;
	uint64_t mAlaggedFrames;
	bool mAudioPending;
	bool mVideoPending;
	AVFrame *mVframe;
	AVFrame *mAframe;
private:
	int64_t getMediaInterval(bool audio);
	void video_sleep(uint64_t *p_time, uint64_t interval_us);
	void audio_sleep(uint64_t *p_time, uint64_t interval_us);
	bool getClosedAudioFrame(uint64_t audio_time, AVFrame *frame,uint64_t interval);
	bool getClosedVideoFrame(uint64_t video_time, AVFrame*frame,uint64_t interval);
	void resetValue()
	{
		mDemuxer = NULL;
		mOutput = NULL;
		mAfilter = NULL;
		mVfilter = NULL;
		mCallback = NULL;
		mVideoThread = NULL;
		mAudioThread = NULL;
		mAbort = NULL;

		mInited = false;
		memset(&mOutcfg,0,sizeof(mOutcfg));

		//for record
		mVtotalFrames = 0;
		mVlaggedFrames = 0;

		mAtotalFrames = 0;
		mAlaggedFrames = 0;
		mAudioPending = 0;
		mVideoPending = 0;
		mVframe = NULL;
		mAframe = NULL;
	}
};

//callback-----------------------------------------------/
class push_callback :public decoder_callback
{
public:
	push_callback(push * _push){ mPush = _push; }
	virtual ~push_callback(){}
public:
	virtual void audio_callback(AVStream *stream, AVCodecContext* codec_ctx, AVFrame * frame);
	virtual void video_callback(AVStream *stream, AVCodecContext* codec_ctx, AVFrame * frame);
	virtual void demuxer_ready_callback();
private:
	push *mPush;
};

void push_callback::audio_callback(AVStream *stream, AVCodecContext* codec_ctx, AVFrame * frame)
{
	mPush->audio_callback(stream,codec_ctx,frame);
}

void push_callback::video_callback(AVStream *stream, AVCodecContext* codec_ctx, AVFrame * frame)
{
	mPush->video_callback(stream,codec_ctx,frame);
}
void push_callback::demuxer_ready_callback()
{
	mPush->demuxer_ready_callback();
}
/*-----------------------------------------------------------------------------------------------*/
//push
push::push(struct push_config *config)
{
	resetValue();
	mConfig = *config;
	mDemuxer = new ff_demuxer();
	mOutput=new ffmpeg_output();
	mAfilter=new audiofilter();
	mVfilter=new videofilter();
	mCallback=new push_callback(this);

	mAudioPending=false;
	mVideoPending=false;
	mAframe = mVframe = NULL;
}
void push::audio_callback(AVStream *stream, AVCodecContext* codec_ctx, AVFrame * frame)
{
	if (mInited)
	    mAfilter->push_frame_back(frame);
}
void push::video_callback(AVStream *stream, AVCodecContext* codec_ctx, AVFrame * frame)
{
	if (mInited)
	   mVfilter->push_frame_back(mDemuxer->mFormat_context, stream, frame);
}

void push::demuxer_ready_callback()
{	
	mOutcfg.audioconfig.channels = mDemuxer->mchannels;
	mOutcfg.audioconfig.sample_rate = mDemuxer->mfreq;
	mOutcfg.audioconfig.sample_fmt = mDemuxer->mfmt;
	mOutcfg.audioconfig.timebase = mDemuxer->mAtimebase;
	mOutcfg.audioconfig.codecId = AV_CODEC_ID_AAC;
	mOutcfg.audioconfig.audio_bitrate = 128 * 1000;

	mOutcfg.videoconfig.video_bitrate = 1000 * 1000;
	mOutcfg.videoconfig.codecId = AV_CODEC_ID_H264;
	mOutcfg.videoconfig.frame_aspect_ratio = mDemuxer->mframe_aspect_ratio;
	mOutcfg.videoconfig.frame_rate = mDemuxer->mframe_rate;
	mOutcfg.videoconfig.height = mDemuxer->mHeight;
	mOutcfg.videoconfig.pix_fmt = AV_PIX_FMT_YUV420P;
	mOutcfg.videoconfig.timebase = mDemuxer->mVtimebase;
	mOutcfg.videoconfig.width = mDemuxer->mWidth;

	mOutcfg.url = mConfig.output;
	mOutcfg.format_name = mConfig.output_format;

	mOutput->start_output(&mOutcfg);

	mAfilter->set_frame_size(mOutput->get_audio_frame_size());
	mVideoThread = new boost::thread(video_thread, this);
	mAudioThread = new boost::thread(audio_thread, this);
	mInited = true;
}

int push::start_push()
{
	mDemuxer->demuxer_open(mConfig.input, mConfig.input_format, mCallback);
	return 0;
}

int push::stop_push()
{
	mAbort = true;
	mDemuxer ->demuxer_close();
	mVideoThread->join();
	mAudioThread->join();
	mOutput ->close_output();
	return 0;
}
int64_t push::getMediaInterval(bool audio)
{
	int64_t interval = 0;
	if (mInited){
		if (!audio)
			interval = mOutcfg.videoconfig.frame_rate.num * 1000000 / mOutcfg.videoconfig.frame_rate.den;
		else
			interval = mOutput->get_audio_frame_size() * 1000000 / mOutcfg.audioconfig.sample_rate;
	}
	return interval;
}

void push::video_sleep(uint64_t *p_time, uint64_t interval_us)
{
	uint64_t cur_time = *p_time;
	uint64_t t = cur_time + interval_us;
	int count;

	int64_t sleep_duration = t - get_sys_time();
	if (sleep_duration > 0) {
		av_usleep(sleep_duration);
		*p_time = t;
		count = 1;
	}
	else {
		count = (int)((get_sys_time() - cur_time) / interval_us);
		*p_time = cur_time + interval_us * count;
	}

	mVtotalFrames += count;
	mVlaggedFrames += count - 1;
}

void push::audio_sleep(uint64_t *p_time, uint64_t interval_us)
{
	uint64_t cur_time = *p_time;
	uint64_t t = cur_time + interval_us;
	int count;

	int64_t sleep_duration = t - get_sys_time();
	if (sleep_duration > 0) {
		av_usleep(sleep_duration);
		*p_time = t;
		count = 1;
	}
	else {
		count = (int)((get_sys_time() - cur_time) / interval_us);
		*p_time = cur_time + interval_us * count;
	}

	mAtotalFrames += count;
	mAlaggedFrames += count - 1;
}

bool push::getClosedAudioFrame(uint64_t audio_time, AVFrame *frame,uint64_t interval)
{
	while(!mAbort){
		if (!mAudioPending&&mAfilter->getFrame(frame) < 0)
			return false;
		else if(mAudioPending){
			av_frame_move_ref(frame, mAframe);
			mAudioPending = false;
		}

		if (!mSync.init_ok())
			mSync.resetSync(frame->pts);

		if (mSync.frame_within_curent(frame->pts, interval)){
			return true;
		}

		if (mSync.frame_without_less(frame->pts, interval))
		{
			av_frame_unref(frame);
			continue;
		}
		else{
			if (!mAframe) mAframe = av_frame_alloc();
			av_frame_move_ref(mAframe, frame);
			mAudioPending = true;
			return false;
		}
		
	}
	return false;
}
bool push::getClosedVideoFrame(uint64_t video_time, AVFrame*frame, uint64_t interval)
{
	while (!mAbort&&mSync.init_ok()){
		if (!mVideoPending&&mVfilter->getFrame(frame) < 0)
			return false;
		else if (mVideoPending){
			av_frame_move_ref(frame, mVframe);
			mVideoPending = false;
		}

	//	if (!mSync.init_ok())
	//		mSync.resetSync(frame->pts);

		if (mSync.frame_within_curent(frame->pts, interval)){
			return true;
		}

		if (mSync.frame_without_less(frame->pts, interval))
		{
			av_frame_unref(frame);
			continue;
		}
		else{
			if (!mVframe) mVframe = av_frame_alloc();
			av_frame_move_ref(mVframe, frame);
			mVideoPending = true;
			return false;
		}

	}
	return false;
}
int push::video_push_thread()
{
	uint64_t interval = getMediaInterval(false);
	if (interval <= 0)
	{
		TRACE("get video interval not right!");
		return 0;
	}
	uint64_t video_time = get_sys_time();
	AVFrame *frame = av_frame_alloc();
	while (!mAbort)
	{
		if (getClosedVideoFrame(video_time, frame,interval))
		{
			mOutput->video_frame(frame);
			av_frame_unref(frame);
		}
		video_sleep(&video_time,interval);
	}
	return 0;
}

int push::audio_push_thread()
{
	uint64_t interval = getMediaInterval(true);
	uint64_t audio_time = get_sys_time();

	AVFrame *frame = av_frame_alloc();
	while (!mAbort)
	{
		if (getClosedAudioFrame(audio_time, frame, interval))
		{
			mOutput->audio_frame(frame);
			av_frame_unref(frame);
		}
		audio_sleep(&audio_time, interval);
	}
	return 0;
}

int push::video_thread(push * _push)
{
	return _push->video_push_thread();
}
int push::audio_thread(push * _push)
{
	return _push->audio_push_thread();
}

int main()
{
	push::push_config config;
	config.input = "video=Integrated Camera:audio=External Mic (Realtek High Defi";
	config.input_format="dshow";

	config.output = "rtmp://10.128.164.55:1990/live/stream_test5";
	config.output_format="flv";
	config.video_codec="libx264";
	config.audio_codec="aac";
	init_ffmpeg();
	push *_push = new push(&config);
	_push->start_push();

	int index = 0;

	while (1)
	{
		index++;
		av_usleep(1000000);
		if (index == 100)
			break;
	}
	_push->stop_push();
	delete _push;
	return 0;
}