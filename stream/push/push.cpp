#include "push.h"

int init_ffmpeg()
{
#if CONFIG_AVDEVICE
	avdevice_register_all();
	av_register_input_format(&ff_dshowaa_demuxer);
#endif
#if CONFIG_AVFILTER
	avfilter_register_all();
#endif
	av_register_all();
	avformat_network_init();
	return 0;
}

//push
push::push(Json::Value & inputsettings, Json::Value & wasapisettings, Json::Value & outputsetting)
{
	mStreamSource = NULL;
	mWasStreamSource = NULL;
	mOutput = NULL;
	mVideoThread = NULL;
	mAudioThread = NULL;
	mVideoDisplay = NULL;
	mAbort = false;
	mInited = false;

	//for record
	mCountVideoFrame = 0;
	mCountlaggedVideoFrames = 0;
	mCountAudioFrame = 0;
	mCountlaggedAudioFrame = 0;


	mAudioPending = false;
	mVideoPending = false;
	mAudioPendingframe = mVideoPendingframe = NULL;

	mInput_JsonSetting = inputsettings;
	mOutput_JsonSetting = outputsetting;
	mWasapi_JsonSetting = wasapisettings;
}

push::~push()
{
	stop_push();
	if (mStreamSource)
		delete mStreamSource;
	if (mWasStreamSource)
		delete mWasStreamSource;
	if (mOutput)
		delete mOutput;
}


int push::start_push()
{
	Json::Value filterOutSetting;
	mStreamSource = CreateSource(mInput_JsonSetting);
	mWasStreamSource = CreateSource(mWasapi_JsonSetting);
	mOutput = new ffmpeg_output(mOutput_JsonSetting);

	//must include below properties
	filterOutSetting["audioSampleFmt"] = AV_SAMPLE_FMT_FLTP;
	filterOutSetting["audioFrameSize"] = 1024;
	filterOutSetting["videoPixFmt"] = AV_PIX_FMT_YUV420P;

	mStreamSource = new MediaFilterSource(mStreamSource, filterOutSetting);

	mStreamSource->activeSourceStream();
	mWasStreamSource->activeSourceStream();
	mOutput->start();

	mVideoThread = new boost_thread(video_thread, this);
	mAudioThread = new boost_thread(audio_thread, this);
	mInited = true;
	return 0;
}

int push::stop_push()
{
	mAbort = true;
	if (mStreamSource){
		mStreamSource->deactiveSourceStream();
		delete mStreamSource;
		mStreamSource = NULL;
	}

	if (mWasStreamSource){
		mWasStreamSource->deactiveSourceStream();
		delete mWasStreamSource;
		mWasStreamSource = NULL;
	}
	if (mVideoThread){
		mVideoThread->join();
		delete mVideoThread;
		mVideoThread = NULL;
	}

	if (mAudioThread){
		mAudioThread->join();
		delete mAudioThread;
		mAudioThread = NULL;
	}

	if (mOutput){
		mOutput->stop();
		delete mOutput;
		mOutput = NULL;
	}

	if (mVideoDisplay){
		delete mVideoDisplay;
		mVideoDisplay = NULL;
	}
	mInited = false;
	return 0;
}
int64_t push::getMediaInterval(bool audio)
{
	int64_t interval = 0;
	if (mInited){
		if (!audio){
			int frame_rate = mInput_JsonSetting["videoFps"].asInt();
			if (!frame_rate) frame_rate = 30;
			interval = 1000000 / frame_rate;
		}
		else{
			int frame_size = 1024;
			int sample_rate = mInput_JsonSetting["audioSampleRate"].asInt();
			if (!sample_rate) sample_rate = 44100;
			interval = frame_size * 1000000 / sample_rate;
		}
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

	mCountVideoFrame += count;
	mCountlaggedVideoFrames += count - 1;
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

	mCountAudioFrame += count;
	mCountlaggedAudioFrame += count - 1;
}

bool push::getClosedAudioFrame(uint64_t audio_time, AVFrame *frame,uint64_t interval)
{
	while(!mAbort){
		if (!mAudioPending){
			if(mStreamSource->getSourceFrame(frame, true) == false)
				return false;
		}
		else if (mAudioPending){
			av_frame_move_ref(frame, mAudioPendingframe);
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
			if (!mAudioPendingframe) mAudioPendingframe = av_frame_alloc();
			av_frame_move_ref(mAudioPendingframe, frame);
			mAudioPending = true;
			return false;
		}
		
	}
	return false;
}
bool push::getClosedVideoFrame(uint64_t video_time, AVFrame*frame, uint64_t interval)
{
	while (!mAbort&&mSync.init_ok()){
		if (!mVideoPending){
			if(mStreamSource->getSourceFrame(frame, false) == false)
				return false;
		}
		else if (mVideoPending){
			av_frame_move_ref(frame, mVideoPendingframe);
			mVideoPending = false;
		}

		//if (!mSync.init_ok())
		//	mSync.resetSync(frame->pts);

		if (mSync.frame_within_curent(frame->pts, interval)){
			return true;
		}

		if (mSync.frame_without_less(frame->pts, interval))
		{
			av_frame_unref(frame);
			continue;
		}
		else{
			if (!mVideoPendingframe) 
				mVideoPendingframe = av_frame_alloc();
			av_frame_move_ref(mVideoPendingframe, frame);
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
			TRACE("push video frame\n");

			if (!mVideoDisplay)
				mVideoDisplay = new videoDisplay();
			mVideoDisplay->showVideoFrame(frame);

			mOutput->raw_video(frame);
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
			mWasStreamSource->rawRender(frame);
			TRACE("push audio frame\n");
			mOutput->raw_audio(frame);
			av_frame_unref(frame);
		}
		audio_sleep(&audio_time, interval);
	}
	return 0;
}

int push::video_thread(void *param)
{
	push * _push = (push *)param;
	return _push->video_push_thread();
}
int push::audio_thread(void *param)
{
	push * _push = (push *)param;
	return _push->audio_push_thread();
}
