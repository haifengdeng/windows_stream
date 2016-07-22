#ifndef FFMPEG_FILTER_HEADER_H
#define FFMPEG_FILTER_HEADER_H
#include"config.h"
#include "stream_source.h"

class  mediafilter{
public:
	mediafilter(){
		mGraph = NULL;
		mSource_ctx = mOutctx = NULL;
	}
	virtual ~mediafilter(){
		if (mGraph){
			avfilter_graph_free(&mGraph);
			mGraph = NULL;
			mSource_ctx = NULL;
			mOutctx = NULL;
		}
	}
	int setGraph(AVFilterGraph*graph,AVFilterContext*Source,AVFilterContext *mOut){
		if (mGraph){
			avfilter_graph_free(&mGraph);
			mGraph = NULL;
		}
		mGraph = graph;
		mSource_ctx = Source;
		mOutctx = mOut;
		return 0;
	}

public:
	std::mutex         mMutex;
	AVFilterGraph *mGraph;
	AVFilterContext *mSource_ctx;
	AVFilterContext *mOutctx;

};

//video filters
class videofilter :public mediafilter{
public:
	struct vfilter_cfg{//input frame format
		enum AVPixelFormat fmt;
		int width;
		int height;
		int framerate;
	};
public:
	videofilter(vfilter_cfg &cfg,AVPixelFormat OutFmt) :mediafilter(){
		mInputCfg = cfg;
		mOutfmt = OutFmt;
		configure_video_filters();
	}


	int push_frame_back(AVFrame *frame);

	int getFrame(AVFrame*frame){
		int ret = -1;
		std::unique_lock<std::mutex> lck(mMutex);
		if (mOutctx){
			ret = av_buffersink_get_frame_flags(mOutctx, frame, 0);
		}
		return ret;
	}
private:
	int configure_video_filters();
	vfilter_cfg mInputCfg;
	enum AVPixelFormat mOutfmt;
};

//audio filters
class audiofilter :public mediafilter{
public:
	struct AudioParams {//input frame format
		int freq;
		int channels;
		enum AVSampleFormat fmt;
	};

public:
	audiofilter(AudioParams &param, AVSampleFormat OutFmt,int frame_size) :mediafilter(){
		mSrcParams = param;
		mOutFmt = OutFmt;
		mFrame_size = frame_size;
		configure_audio_filters();
	}

	int push_frame_back(AVFrame *frame);
	int getFrame(AVFrame*frame){
		int ret = -1;
		std::unique_lock<std::mutex> lck(mMutex);
		if (mOutctx){
			ret = av_buffersink_get_frame_flags(mOutctx, frame, 0);
			if (ret>=0)
			  frame->pts = av_rescale_q(frame->pts, mOutctx->inputs[0]->time_base, std_tb_us);
		}
		return ret;
	}
public:
	int configure_audio_filters();
	AudioParams mSrcParams;
	enum AVSampleFormat mOutFmt;
	int mFrame_size;
};


class MediaFilterSource :public stream_source
{
public:
	MediaFilterSource(stream_source* source,Json::Value & outFmt);
	~MediaFilterSource();

	//stream source interface
	virtual void getSourceStringId(std::string & id);
	virtual int getSourceType();
	virtual int getSourceFlag();
	virtual int activeSourceStream();
	virtual int deactiveSourceStream();
	virtual int updateSourceSetting(Json::Value & info);
	virtual void getSourceProperties(Json::Value &props,propertytype flag);
	virtual bool getSourceFrame(AVFrame* frame, bool audio);
	virtual AVFrame * getSourceFrame(bool audio) ;
	bool isactive();
private:
    void CreateAudioFilter();
	void CreateVideoFilter();
private:
	audiofilter   *mAudiofilter;
	videofilter   *mVideofilter;
	stream_source * mSource;

	//audio
	audiofilter::AudioParams mInputAudioParams;
	enum AVSampleFormat mOutAudioFmt;
	int mOutAudioFrameSize;
	//video
	videofilter::vfilter_cfg mInputVideoParams;
	enum AVPixelFormat mOutVideofmt;

	//
	bool mActived;
};
#endif