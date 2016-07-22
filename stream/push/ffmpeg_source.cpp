#include "input.h"
#include "dshow\Dshow_monitor.h"



class ffmpegSource_callback :public source_callback
{
public:
	ffmpegSource_callback(ffmpeg_source * source){ mSource = source; }
	virtual ~ffmpegSource_callback(){}
public:
	virtual void audio_callback(AVFrame * frame);
	virtual void video_callback(AVFrame * frame);
private:
	ffmpeg_source *mSource;
};

void ffmpegSource_callback::audio_callback(AVFrame * frame)
{
	if (AV_NOPTS_VALUE == frame->pts)
		TRACE("frame timestamps not valuable\n");
	mSource->audio_callback( frame);
}

void ffmpegSource_callback::video_callback(AVFrame * frame)
{
	if (AV_NOPTS_VALUE == frame->pts)
		TRACE("frame timestamps not valuable\n");

	mSource->video_callback(frame);
}

/*********************************************************************************/
void ffmpeg_source::getSourceDefaultSetting(Json::Value &setting)
{
	Dshow_monitor monitor;
	monitor.chooseAudioDeviceAndConfig(setting);
	monitor.chooseVideoDeviceAndConfig(setting);
}
ffmpeg_source::ffmpeg_source(Json::Value & info)
{
	mDemuxer = NULL;
	mActived = false;
	mCallback = NULL;
	memset(&mInputCfg, 0, sizeof(mInputCfg));

	std::string input = "video=" + info["videoDeviceName"].asString() +
		                ":audio=" + info["audioDeviceName"].asString();
	mInputCfg.input = av_strdup(input.c_str());
	mInputCfg.input_format = av_strdup(info["inputFmt"].asString().c_str());
	//audio
	mInputCfg.channel = info["audioChannel"].asInt();
	mInputCfg.frequency = info["audioSampleRate"].asInt();
	mInputCfg.bitsPerSample = info["audioBitsPerSample"].asInt();
	//video
	mInputCfg.pix_fmt = (AVPixelFormat)info["videoPixFmt"].asInt();
	mInputCfg.width = info["videoWidth"].asInt();
	mInputCfg.height = info["videoHeight"].asInt();
	mInputCfg.Fps = info["videoFps"].asInt();
}
ffmpeg_source::~ffmpeg_source()
{
	if (mDemuxer){
		mDemuxer->stopDemuxer();
		delete mDemuxer;
		mDemuxer = NULL;
	}
	
	av_freep(&mInputCfg.input);
	av_freep(&mInputCfg.input_format);
}

void ffmpeg_source::getSourceStringId(std::string & id)
{
	id = "ffmpeg source";
}
int ffmpeg_source::getSourceType()
{
	return stream_source::source_type::SOURCE_TYPE_INPUT;
}
int ffmpeg_source::getSourceFlag()
{
	return SOURCE_AUDIO|SOURCE_VIDEO;
}

int ffmpeg_source::activeSourceStream()
{
	if (!mCallback)
		mCallback = new ffmpegSource_callback(this);

	if (!mDemuxer)
		mDemuxer = new ff_demuxer(&mInputCfg,mCallback);


	mDemuxer->startDemuxer();
	mActived = true;
	return 0;
}
int ffmpeg_source::deactiveSourceStream()
{
	if (mDemuxer){
		mDemuxer->stopDemuxer();
		delete mDemuxer;
		mDemuxer = NULL;
	}
	mActived = false;
	return 0;
}

bool ffmpeg_source::isactive()
{
	return mActived;
}
int ffmpeg_source::updateSourceSetting(Json::Value & info)
{
	return 0;
}

AVSampleFormat av_get_sample_format(int bitsPerSample)
{
	if (bitsPerSample == 8)
		return AV_SAMPLE_FMT_U8;
	if (bitsPerSample == 16)
		return AV_SAMPLE_FMT_S16;
}
void ffmpeg_source::getSourceProperties(Json::Value &props, propertytype flag)
{
	if (!mActived)
		return;

	if (flag & PROPERTYTYPE_AUDIO){
		props["audioSampleRate"] = mInputCfg.frequency;
		props["audioChannel"] = mInputCfg.channel;
		props["audioSampleFmt"] = av_get_sample_format(mInputCfg.bitsPerSample);
	}

	if (flag & PROPERTYTYPE_VIDEO){
		props["videoPixFmt"] = mInputCfg.pix_fmt;
		props["videoWidth"] = mInputCfg.width;
		props["videoHeight"] = mInputCfg.height;
		props["videoFps"] = mInputCfg.Fps;
	}
}

void ffmpeg_source::audio_callback(AVFrame * frame)
{
	mMutex.lock();
	TRACE("audio_callback:push audio frame\n");
	mAudioFrameQueue.push(frame);
	mMutex.unlock();
}
void ffmpeg_source::video_callback(AVFrame * frame)
{
	mMutex.lock();
	TRACE("video_callback:push video frame\n");
	mVideoFrameQueue.push(frame);
	mMutex.unlock();
}

AVFrame* ffmpeg_source::getSourceFrame(bool audio)
{
	AVFrame * frame = NULL;
	mMutex.lock();
	if (audio && (mAudioFrameQueue.size()>0)){
		frame = mAudioFrameQueue.front();
		mAudioFrameQueue.pop();
	}else if(!audio && mVideoFrameQueue.size()){
		frame = mVideoFrameQueue.front();
		mVideoFrameQueue.pop();
	}
	mMutex.unlock();
	return frame;
}

bool ffmpeg_source::getSourceFrame(AVFrame* frame, bool audio)
{
	assert(false);
	return false;
}