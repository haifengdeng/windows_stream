#ifndef FFMPEG_FILTER_HEADER_H
#define FFMPEG_FILTER_HEADER_H
#include"config.h"

class  mediafilter{
public:
	mediafilter(){
		mGraph = NULL;
		mSource_ctx = mSink_ctx = mOutctx = NULL;
	}
	virtual ~mediafilter(){
		if (mGraph){
			avfilter_graph_free(&mGraph);
			mGraph = NULL;
			mSource_ctx = NULL;
			mSink_ctx = NULL;
		}
	}
	int setGraph(AVFilterGraph*graph){
		if (mGraph){
			avfilter_graph_free(&mGraph);
			mGraph = NULL;
		}
		mGraph = graph;
		return 0;
	}
	int configGraph(const char*filtergraph){
		if (NULL == mGraph || NULL == mSource_ctx || NULL == mSink_ctx){
			return -1;
		}
		return configure_filtergraph(mGraph, filtergraph, mSource_ctx, mSink_ctx);
	}
private:
	int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
		AVFilterContext *source_ctx, AVFilterContext *sink_ctx);
public:
	boost::mutex         mMutex;
	AVFilterGraph *mGraph;
	AVFilterContext *mSource_ctx;
	AVFilterContext *mSink_ctx;
	AVFilterContext *mOutctx;

};

//video filters
class videofilter :public mediafilter{
public:
	struct vfilter_cfg{//input frame format
		enum AVPixelFormat fmt;
		int width;
		int height;
		AVRational framerate;
		AVRational sample_aspect_ratio;
		AVRational time_base;
	};
public:
	videofilter() :mediafilter(){
		memset(&mLast_cfg, 0, sizeof(mLast_cfg));
		mOutfmt = AV_PIX_FMT_NONE;
		autorotate = bcrop = false;
	}
	int configure_video_filters(vfilter_cfg * cfg, const char *vfilters);

	int push_frame_back(AVFormatContext *format_ctx, AVStream *stream, AVFrame *frame);
	int getFrame(AVFrame*frame){
		int ret = -1;
		boost::unique_lock<boost::mutex> lck(mMutex);
		if (mOutctx){
			ret = av_buffersink_get_frame_flags(mOutctx, frame, 0);
		}
		return ret;
	}
private:
	vfilter_cfg mLast_cfg;
	enum AVPixelFormat mOutfmt;
	bool autorotate;
	bool bcrop;
};

//audio filters
class audiofilter :public mediafilter{
public:
	struct AudioParams {//input frame format
		int freq;
		int channels;
		int64_t channel_layout;
		enum AVSampleFormat fmt;
		int bytes_per_sec;
	};

public:
	audiofilter() :mediafilter(){
		memset(&mSrcParams, 0, sizeof(mSrcParams));
		memset(&mTgParams, 0, sizeof(mTgParams));
		mOutFmt = AV_SAMPLE_FMT_NONE;
		mFrame_size = 0;
	}
	void set_frame_size(int frame_size)
	{
		mFrame_size = frame_size;
	}
	int configure_audio_filters(AudioParams *cfg, const char *afilters, int force_output_format);
	int push_frame_back(AVFrame *frame);
	int getFrame(AVFrame*frame){
		int ret = -1;
		boost::unique_lock<boost::mutex> lck(mMutex);
		if (mOutctx){
			ret = av_buffersink_get_frame_flags(mOutctx, frame, 0);
			if (ret>=0)
			  frame->pts = av_rescale_q(frame->pts, mOutctx->inputs[0]->time_base, std_tb_us);
		}
		return ret;
	}
public:
	AudioParams mSrcParams;
	AudioParams mTgParams;
	enum AVSampleFormat mOutFmt;
	int mFrame_size;
};
#endif