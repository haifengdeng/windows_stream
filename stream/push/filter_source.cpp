#include "filter.h"

MediaFilterSource::MediaFilterSource(stream_source* source, Json::Value & outFmt)
{
	mSource = source;
	mOutAudioFmt = (AVSampleFormat) outFmt["audioSampleFmt"].asInt();
	mOutAudioFrameSize = outFmt["audioFrameSize"].asInt();
	mOutVideofmt = (AVPixelFormat)outFmt["videoPixFmt"].asInt();

	mAudiofilter = NULL;
	mVideofilter = NULL;
	memset(&mInputAudioParams, 0, sizeof(mInputAudioParams));
	memset(&mInputVideoParams, 0, sizeof(mInputVideoParams));
	mActived = false;
}
MediaFilterSource::~MediaFilterSource()
{
	mSource->deactiveSourceStream();
	if (mAudiofilter)
		delete mAudiofilter;
	mAudiofilter = NULL;
	if (mVideofilter)
		delete mVideofilter;
	mVideofilter = NULL;
}

void MediaFilterSource::CreateAudioFilter()
{
	if (!mSource->isactive() || mAudiofilter)
		 return;

	Json::Value json;
	mSource->getSourceProperties(json,stream_source::PROPERTYTYPE_AUDIO);
    
	mInputAudioParams.channels = json["audioChannel"].asInt();
	mInputAudioParams.freq = json["audioSampleRate"].asInt();
	mInputAudioParams.fmt = (AVSampleFormat)json["audioSampleFmt"].asInt();

	mAudiofilter = new audiofilter(mInputAudioParams,mOutAudioFmt,mOutAudioFrameSize);
}

void MediaFilterSource::CreateVideoFilter()
{
	if (!mSource->isactive() || mVideofilter)
		return;

	Json::Value json;
	mSource->getSourceProperties(json, stream_source::PROPERTYTYPE_VIDEO);

	mInputVideoParams.fmt = (AVPixelFormat)json["videoPixFmt"].asInt();
	mInputVideoParams.width = json["videoWidth"].asInt();
	mInputVideoParams.height = json["videoHeight"].asInt();
	mInputVideoParams.framerate = json["videoFps"].asInt();

	mVideofilter = new videofilter(mInputVideoParams, mOutVideofmt);
}
//stream source interface
void MediaFilterSource::getSourceStringId(std::string & id)
{
	id = "media filter source";
}
int MediaFilterSource::getSourceType()
{
	return stream_source::source_type::SOURCE_TYPE_FILTER;
}
int MediaFilterSource::getSourceFlag()
{
	return mSource->getSourceFlag();
}
int MediaFilterSource::activeSourceStream()
{
	int ret = 0;
	if (!mSource->isactive())
	   ret = mSource->activeSourceStream();
	mActived = true;
	return ret;
}
int MediaFilterSource::deactiveSourceStream()
{
	mSource->deactiveSourceStream();
	mActived = false;
	return 0;
}
bool MediaFilterSource::isactive()
{
	return mActived;
}
int MediaFilterSource::updateSourceSetting(Json::Value & info)
{
	return mSource->updateSourceSetting(info);
}
void MediaFilterSource::getSourceProperties(Json::Value &props,propertytype flag)
{
	mSource->getSourceProperties(props, flag);
	if (flag & PROPERTYTYPE_AUDIO){
		props["audioSampleFmt"] = mOutAudioFmt;
		props["audioFrameSize"] = mOutAudioFrameSize;
	}
	if (flag & PROPERTYTYPE_VIDEO){
		props["videoPixFmt"] = mOutVideofmt;
	}
}

 bool MediaFilterSource::getSourceFrame(AVFrame* frame,bool audio)
{
	int ret = -1;
	AVFrame *temp;

	if (audio &&!mAudiofilter)
		CreateAudioFilter();
	else if (!audio && !mVideofilter)
		CreateVideoFilter();
Resart:
	if (audio &&mAudiofilter)
		ret=mAudiofilter->getFrame(frame);
	else if (!audio && mVideofilter)
		ret = mVideofilter->getFrame(frame);
	else
		return false;

	if (ret < 0 && mSource->isactive() &&(temp = mSource->getSourceFrame(audio))){
		if (audio && mAudiofilter)
			mAudiofilter->push_frame_back(temp);
		else if (!audio && mVideofilter)
			mVideofilter->push_frame_back(temp);
		goto Resart;
	}

	if (ret < 0)
		return false;
	return true;

}

 AVFrame * MediaFilterSource::getSourceFrame(bool audio)
 {
	 assert(false);
	 return NULL;
 }