#include "filter.h"


 int videofilter::configure_video_filters()
{
	enum AVPixelFormat pix_fmts[] = { mOutfmt, AV_PIX_FMT_NONE };
	char sws_flags_str[512] = "";
	char buffersrc_args[256] = "";
	int ret=0;
	AVFilterContext *filt_src = NULL, *filt_out = NULL, *last_filter = NULL;
	AVDictionaryEntry *e = NULL;
	AVFilterGraph* graph = avfilter_graph_alloc();

	if (strlen(sws_flags_str))
		sws_flags_str[strlen(sws_flags_str) - 1] = '\0';

	graph->scale_sws_opts = av_strdup(sws_flags_str);

	snprintf(buffersrc_args, sizeof(buffersrc_args),
		"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
		mInputCfg.width, mInputCfg.height, mInputCfg.fmt,
		std_tb_us.num, std_tb_us.den,
		0, 1);
	if (mInputCfg.framerate)
		av_strlcatf(buffersrc_args, sizeof(buffersrc_args), ":frame_rate=%d/%d", 
		            mInputCfg.framerate, 1);

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

	if((ret = avfilter_link(filt_src, 0, last_filter, 0)) < 0)
		goto fail;
	if ((ret = avfilter_graph_config(graph, NULL)) < 0)
		goto fail;

	setGraph(graph,filt_src,filt_out);
	return 0;
fail:
	avfilter_graph_free(&graph);
	return ret;
}

 int videofilter::push_frame_back(AVFrame *frame)
 {
	 int ret = 0;
	 vfilter_cfg  cfg;
	 std::unique_lock<std::mutex> lck(mMutex);

	 if (NULL == mGraph){
		 if ((ret = configure_video_filters()) < 0) {
			 goto the_end;
		 }
	 }

	 ret = av_buffersrc_add_frame(mSource_ctx, frame);
	 if (ret < 0)
		 goto the_end;
 the_end:
	 return ret;
 }

int audiofilter::configure_audio_filters()
{
	enum AVSampleFormat sample_fmts[] = { mOutFmt, AV_SAMPLE_FMT_NONE };
	AVFilterContext *filt_asrc = NULL, *filt_asink = NULL;
	char aresample_swr_opts[512] = "";
	AVDictionaryEntry *e = NULL;
	char asrc_args[256];
	int ret;
	AVFilterGraph *graph = NULL;

	if (!(graph = avfilter_graph_alloc()))
		return AVERROR(ENOMEM);

	if (strlen(aresample_swr_opts))
		aresample_swr_opts[strlen(aresample_swr_opts) - 1] = '\0';
	av_opt_set(graph, "aresample_swr_opts", aresample_swr_opts, 0);

	ret = snprintf(asrc_args, sizeof(asrc_args),
		"sample_rate=%d:sample_fmt=%s:channels=%d:time_base=%d/%d",
		mSrcParams.freq, av_get_sample_fmt_name(mSrcParams.fmt),
		mSrcParams.channels,
		std_tb_us.num, std_tb_us.den);

	ret = avfilter_graph_create_filter(&filt_asrc,
		avfilter_get_by_name("abuffer"), "push_abuffer",
		asrc_args, NULL, graph);
	if (ret < 0)
		goto fail;


	ret = avfilter_graph_create_filter(&filt_asink,
		avfilter_get_by_name("abuffersink"), "push_abuffersink",
		NULL, NULL, graph);
	if (ret < 0)
		goto fail;

	if ((ret = av_opt_set_int_list(filt_asink, "sample_fmts", sample_fmts, AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN)) < 0)
		goto fail;
	if ((ret = av_opt_set_int(filt_asink, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN)) < 0)
		goto fail;

	if ((ret = avfilter_link(filt_asrc, 0, filt_asink, 0)) < 0)
		goto fail;
	if ((ret = avfilter_graph_config(graph, NULL)) < 0)
		goto fail;

	av_buffersink_set_frame_size(filt_asink, 1024);

	setGraph(graph, filt_asrc, filt_asink);
	return 0;
fail:
	avfilter_graph_free(&graph);
	return ret;
}

int audiofilter::push_frame_back(AVFrame *frame)
{
	int ret = 0;
	AudioParams cfg;
	std::unique_lock<std::mutex> lck(mMutex);

	if (NULL == mGraph){
		if ((ret = configure_audio_filters()) < 0) {
			goto the_end;
		}
	}
	if ((ret = av_buffersrc_add_frame(mSource_ctx, frame)) < 0)
		goto the_end;
the_end:
	return ret;
}

