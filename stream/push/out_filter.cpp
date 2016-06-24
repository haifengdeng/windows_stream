#include "filter.h"

int mediafilter::configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,
	AVFilterContext *source_ctx, AVFilterContext *sink_ctx)
{
	int ret=0, i=0;
	int nb_filters = graph->nb_filters;
	AVFilterInOut *outputs = NULL, *inputs = NULL;

	if (filtergraph) {
		outputs = avfilter_inout_alloc();
		inputs = avfilter_inout_alloc();
		if (!outputs || !inputs) {
			ret = AVERROR(ENOMEM);
			goto fail;
		}

		outputs->name = av_strdup("in");
		outputs->filter_ctx = source_ctx;
		outputs->pad_idx = 0;
		outputs->next = NULL;

		inputs->name = av_strdup("out");
		inputs->filter_ctx = sink_ctx;
		inputs->pad_idx = 0;
		inputs->next = NULL;

		if ((ret = avfilter_graph_parse_ptr(graph, filtergraph, &inputs, &outputs, NULL)) < 0)
			goto fail;
	}
	else {
		if ((ret = avfilter_link(source_ctx, 0, sink_ctx, 0)) < 0)
			goto fail;
	}

	/* Reorder the filters to ensure that inputs of the custom filters are merged first */
	for (i = 0; i < graph->nb_filters - nb_filters; i++)
		FFSWAP(AVFilterContext*, graph->filters[i], graph->filters[i + nb_filters]);

	ret = avfilter_graph_config(graph, NULL);
fail:
	avfilter_inout_free(&outputs);
	avfilter_inout_free(&inputs);
	return ret;
}

//video filters
int setVideofilter_cfg(videofilter::vfilter_cfg * cfg, AVFormatContext *format_ctx,
	AVStream *stream,AVFrame *frame)
{
	AVCodecContext *codec = stream->codec;
	cfg->framerate = av_guess_frame_rate(format_ctx, stream, NULL);
	cfg->sample_aspect_ratio = codec->sample_aspect_ratio;
	cfg->time_base = stream->time_base;
	cfg->width = frame->width;
	cfg->height = frame->height;
	cfg->fmt = (enum AVPixelFormat)frame->format;
	return true;
}

 int videofilter::configure_video_filters(videofilter::vfilter_cfg *cfg, 
	 const char *vfilters)
{
	static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
	char sws_flags_str[512] = "";
	char buffersrc_args[256] = "";
	int ret=0;
	AVFilterContext *filt_src = NULL, *filt_out = NULL, *last_filter = NULL;
	AVDictionaryEntry *e = NULL;
	AVFilterGraph* graph = avfilter_graph_alloc();
	setGraph(graph);


	if (strlen(sws_flags_str))
		sws_flags_str[strlen(sws_flags_str) - 1] = '\0';

	graph->scale_sws_opts = av_strdup(sws_flags_str);

	snprintf(buffersrc_args, sizeof(buffersrc_args),
		"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
		cfg->width, cfg->height, cfg->fmt,
		cfg->time_base.num, cfg->time_base.den,
		cfg->sample_aspect_ratio.num, FFMAX(cfg->sample_aspect_ratio.den, 1));
	if (cfg->framerate.num && cfg->framerate.den)
		av_strlcatf(buffersrc_args, sizeof(buffersrc_args), ":frame_rate=%d/%d", 
		                  cfg->framerate.num, cfg->framerate.den);

	if ((ret = avfilter_graph_create_filter(&filt_src,
		avfilter_get_by_name("buffer"),
		"push_buffer", buffersrc_args, NULL,
		graph)) < 0)
		goto fail;

	ret = avfilter_graph_create_filter(&filt_out,
		avfilter_get_by_name("buffersink"),
		"push_buffersink", NULL, NULL, graph);
	if (ret < 0)
		goto fail;

	if ((ret = av_opt_set_int_list(filt_out, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
		goto fail;

	last_filter = filt_out;
#if 0
	/* Note: this macro adds a filter before the lastly added filter, so the
	* processing order of the filters is in reverse */
#define INSERT_FILT(name, arg) do {                                          \
    AVFilterContext *filt_ctx;                                               \
                                                                             \
    ret = avfilter_graph_create_filter(&filt_ctx,                            \
                                       avfilter_get_by_name(name),           \
                                       "push_" name, arg, NULL, graph);    \
    if (ret < 0)                                                             \
        goto fail;                                                           \
                                                                             \
    ret = avfilter_link(filt_ctx, 0, last_filter, 0);                        \
    if (ret < 0)                                                             \
        goto fail;                                                           \
                                                                             \
    last_filter = filt_ctx;                                                  \
	} while (0)

	/* SDL YUV code is not handling odd width/height for some driver
	* combinations, therefore we crop the picture to an even width/height. */
	if (bcrop)
	  INSERT_FILT("crop", "floor(in_w/2)*2:floor(in_h/2)*2");

	if (autorotate) {
		double theta = get_rotation(is->video_st);

		if (fabs(theta - 90) < 1.0) {
			INSERT_FILT("transpose", "clock");
		}
		else if (fabs(theta - 180) < 1.0) {
			INSERT_FILT("hflip", NULL);
			INSERT_FILT("vflip", NULL);
		}
		else if (fabs(theta - 270) < 1.0) {
			INSERT_FILT("transpose", "cclock");
		}
		else if (fabs(theta) > 1.0) {
			char rotate_buf[64];
			snprintf(rotate_buf, sizeof(rotate_buf), "%f*PI/180", theta);
			INSERT_FILT("rotate", rotate_buf);
		}
	}
#endif
	mSource_ctx = filt_src;
	mSink_ctx=last_filter;
	mOutctx = filt_out;
	mOutfmt = AV_PIX_FMT_YUV420P;
	mLast_cfg = *cfg;
	if ((ret = configGraph(vfilters) < 0))
		goto fail;
	return 0;

fail:
	return ret;
}

 int videofilter::push_frame_back(AVFormatContext *format_ctx,
	 AVStream *stream, AVFrame *frame)
 {
	 int ret = 0;
	 vfilter_cfg  cfg;
	 setVideofilter_cfg(&cfg, format_ctx, stream, frame);

	 bool reconfigure = cfg.width != mLast_cfg.width
		 || cfg.height != mLast_cfg.height
		 || cfg.fmt != mLast_cfg.fmt;
	 if (reconfigure)
	 {
		 av_log(NULL, AV_LOG_DEBUG,
			 "Video frame changed from size:%dx%d format:%s to size:%dx%d format:%s\n",
			 mLast_cfg.width, mLast_cfg.height,
			 (const char *)av_x_if_null(av_get_pix_fmt_name(mLast_cfg.fmt), "none"),
			 cfg.width, cfg.height,
			 (const char *)av_x_if_null(av_get_pix_fmt_name(cfg.fmt), "none"));
		 if ((ret = configure_video_filters(&cfg, NULL)) < 0) {
			 goto the_end;
		 }
	 }

	 ret = av_buffersrc_add_frame(mSource_ctx, frame);
	 if (ret < 0)
		 goto the_end;
 the_end:
	 return ret;
 }


 //audio filters
int cmp_audio_fmts(enum AVSampleFormat fmt1, int64_t channel_count1,
                   enum AVSampleFormat fmt2, int64_t channel_count2)
{
	/* If channel count == 1, planar and non-planar formats are the same */
	if (channel_count1 == 1 && channel_count2 == 1)
		return av_get_packed_sample_fmt(fmt1) != av_get_packed_sample_fmt(fmt2);
	else
		return channel_count1 != channel_count2 || fmt1 != fmt2;
}

int64_t get_valid_channel_layout(int64_t channel_layout, int channels)
{
	if (channel_layout && av_get_channel_layout_nb_channels(channel_layout) == channels)
		return channel_layout;
	else
		return 0;
}

int setAudioFilterCfg(audiofilter::AudioParams * cfg, AVFrame *frame)
{
	int64_t dec_channel_layout = get_valid_channel_layout(frame->channel_layout, av_frame_get_channels(frame));

	cfg->fmt = (enum AVSampleFormat)frame->format;
	cfg->channels = av_frame_get_channels(frame);
	cfg->channel_layout = dec_channel_layout;
	cfg->freq = frame->sample_rate;
	return true;
}
int initAudioFilterCfg(audiofilter::AudioParams*cfg, AVCodecContext *avctx)
{

	cfg->freq = avctx->sample_rate;
	cfg->channels = avctx->channels;
	cfg->channel_layout = get_valid_channel_layout(avctx->channel_layout, avctx->channels);
	cfg->fmt = avctx->sample_fmt;
	return true;
}
int audiofilter::configure_audio_filters(audiofilter::AudioParams *audio_filter_src, const char *afilters, int force_output_format)
{
	static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE };
	int sample_rates[2] = { 0, -1 };
	int64_t channel_layouts[2] = { 0, -1 };
	int channels[2] = { 0, -1 };
	AVFilterContext *filt_asrc = NULL, *filt_asink = NULL;
	char aresample_swr_opts[512] = "";
	AVDictionaryEntry *e = NULL;
	char asrc_args[256];
	int ret;
	AVFilterGraph *graph = NULL;

	if (!(graph = avfilter_graph_alloc()))
		return AVERROR(ENOMEM);
	setGraph(graph);

	if (strlen(aresample_swr_opts))
		aresample_swr_opts[strlen(aresample_swr_opts) - 1] = '\0';
	av_opt_set(graph, "aresample_swr_opts", aresample_swr_opts, 0);

	ret = snprintf(asrc_args, sizeof(asrc_args),
		"sample_rate=%d:sample_fmt=%s:channels=%d:time_base=%d/%d",
		audio_filter_src->freq, av_get_sample_fmt_name(audio_filter_src->fmt),
		audio_filter_src->channels,
		1, audio_filter_src->freq);
	if (audio_filter_src->channel_layout)
		snprintf(asrc_args + ret, sizeof(asrc_args) - ret,
		":channel_layout=0x%"PRIx64, audio_filter_src->channel_layout);

	ret = avfilter_graph_create_filter(&filt_asrc,
		avfilter_get_by_name("abuffer"), "push_abuffer",
		asrc_args, NULL, graph);
	if (ret < 0)
		goto end;


	ret = avfilter_graph_create_filter(&filt_asink,
		avfilter_get_by_name("abuffersink"), "push_abuffersink",
		NULL, NULL, graph);
	if (ret < 0)
		goto end;

	if ((ret = av_opt_set_int_list(filt_asink, "sample_fmts", sample_fmts, AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
		goto end;
	if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
		goto end;

	if (force_output_format) {
		channel_layouts[0] = mTgParams.channel_layout;
		channels[0] = mTgParams.channels;
		sample_rates[0] = mTgParams.freq;
		if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 0, AV_OPT_SEARCH_CHILDREN)) < 0)
			goto end;
		if ((ret = av_opt_set_int_list(filt_asink, "channel_layouts", channel_layouts, -1, AV_OPT_SEARCH_CHILDREN)) < 0)
			goto end;
		if ((ret = av_opt_set_int_list(filt_asink, "channel_counts", channels, -1, AV_OPT_SEARCH_CHILDREN)) < 0)
			goto end;
		if ((ret = av_opt_set_int_list(filt_asink, "sample_rates", sample_rates, -1, AV_OPT_SEARCH_CHILDREN)) < 0)
			goto end;
	}

	mSource_ctx = filt_asrc;
	mSink_ctx = mOutctx = filt_asink;
	mSrcParams = *audio_filter_src;
	mOutFmt = AV_SAMPLE_FMT_S16;
	if ((ret = configGraph(afilters)) < 0)
		goto end;

	av_buffersink_set_frame_size(mOutctx,1024);

end:
	return ret;
}

int audiofilter::push_frame_back(AVFrame *frame)
{
	int ret = 0;
	AudioParams cfg;
	setAudioFilterCfg(&cfg, frame);

	bool reconfigure =
		cmp_audio_fmts(mSrcParams.fmt, mSrcParams.channels,cfg.fmt, cfg.channels) ||
		mSrcParams.channel_layout != cfg.channel_layout ||
		mSrcParams.freq != frame->sample_rate;

	if (reconfigure) {
		char buf1[1024], buf2[1024];
		av_get_channel_layout_string(buf1, sizeof(buf1), -1, mSrcParams.channel_layout);
		av_get_channel_layout_string(buf2, sizeof(buf2), -1, cfg.channel_layout);
		av_log(NULL, AV_LOG_DEBUG,
			"Audio frame changed from rate:%d ch:%d fmt:%s layout:%s to rate:%d ch:%d fmt:%s layout:%s\n",
			mSrcParams.freq, mSrcParams.channels, av_get_sample_fmt_name(mSrcParams.fmt), buf1,
			cfg.freq, cfg.channels, av_get_sample_fmt_name(cfg.fmt), buf2);

		if ((ret = configure_audio_filters(&cfg, NULL, 1)) < 0)
			goto the_end;
	}

	if ((ret = av_buffersrc_add_frame(mSink_ctx, frame)) < 0)
		goto the_end;

the_end:
	return ret;
}

