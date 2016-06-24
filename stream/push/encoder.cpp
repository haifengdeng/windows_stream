#include "output.h"

void ffmpeg_data::reset_ffmpeg_data()
{
	mVideoStream = mAudioStream = NULL;
	mAcodec = mVcodec = NULL;
	mAudioCodecCtx = mVideoCodecCtx = NULL;
	mOutput = NULL;

	mSwscale = NULL;
	mTotal_frames = 0;
	mVframe = NULL;
	mAFrame_size = 0;

	mTotal_samples = 0;
	mAframe = NULL;
	mAudioSwrCtx = NULL;
	mInitialized = false;
	memset(&mConfig, 0,sizeof(struct ffmpeg_cfg));
}
ffmpeg_data::ffmpeg_data()
{
	reset_ffmpeg_data();
}

bool ffmpeg_data::pixel_fmt_support(const AVCodec * codec, enum AVPixelFormat pix_fmt)
{
	if (codec->pix_fmts) {
		int i = 0;
		for (i = 0; codec->pix_fmts[i] != AV_PIX_FMT_NONE; i++)
		if (pix_fmt == codec->pix_fmts[i])
			break;
		if (codec->pix_fmts[i] == AV_PIX_FMT_NONE){
			return 0;
		}
		return 1;
	}else{
		return 0;
	}
	return 0;
}

bool ffmpeg_data::open_video_codec()
{
	int ret = 0;
	AVDictionary *codec_opts=NULL;
	if (strcmp(mVcodec->name, "libx264") == 0){
		av_opt_set(mVideoCodecCtx->priv_data, "preset", "veryfast", 0);
		av_opt_set(mVideoCodecCtx->priv_data, "tune", "zerolatency", 0);
		//av_dict_set(codec_opts, "profile", "baseline", 0);
	}
	av_dict_set(&codec_opts, "b", "500k", 0);		
	av_dict_set(&codec_opts, "threads", "auto", 0);

	if ((ret = avcodec_open2(mVideoCodecCtx, mVcodec, &codec_opts)) < 0) {
		av_log(NULL, AV_LOG_FATAL,
			"Error open video codec context.\n");
		return ret;
	}
	if (mVideoCodecCtx->bit_rate && mVideoCodecCtx->bit_rate < 1000){
		av_log(NULL, AV_LOG_WARNING, "The bitrate parameter is set too low."
			" It takes bits/s as argument, not kbits/s\n");
	}
	if (mVideoStream->codec != mVideoCodecCtx){
		ret = avcodec_copy_context(mVideoStream->codec, mVideoCodecCtx);
		if (ret < 0) {
			av_log(NULL, AV_LOG_FATAL,
				"Error initializing the output stream codec context.\n");
			return ret;
		}
		mVideoStream->codec->codec = mVideoCodecCtx->codec;
	}

	if (mVideoCodecCtx->nb_coded_side_data) {
		int i;

		mVideoStream->side_data = (AVPacketSideData*)av_realloc_array(NULL, mVideoCodecCtx->nb_coded_side_data,
			sizeof(*mVideoStream->side_data));
		if (!mVideoStream->side_data)
			return AVERROR(ENOMEM);

		for (i = 0; i < mVideoCodecCtx->nb_coded_side_data; i++) {
			const AVPacketSideData *sd_src = &mVideoCodecCtx->coded_side_data[i];
			AVPacketSideData *sd_dst = &mVideoStream->side_data[i];

			sd_dst->data = (uint8_t*)av_malloc(sd_src->size);
			if (!sd_dst->data)
				return AVERROR(ENOMEM);
			memcpy(sd_dst->data, sd_src->data, sd_src->size);
			sd_dst->size = sd_src->size;
			sd_dst->type = sd_src->type;
			mVideoStream->nb_side_data++;
		}
	}
	// copy timebase while removing common factors
	AVRational rational = { 0, 1 };
	mVideoStream->time_base = av_add_q(mVideoCodecCtx->time_base, rational);
	return true;
}

bool ffmpeg_data::sample_fmt_support(const AVCodec* codec, enum AVSampleFormat fmt)
{
	if (codec->sample_fmts) {
		int i = 0;
		for (i = 0; codec->sample_fmts[i] != AV_SAMPLE_FMT_NONE; i++) {
			if (fmt == codec->sample_fmts[i])
				break;
		}
		if (codec->sample_fmts[i] == AV_SAMPLE_FMT_NONE) {
			return 0;
		}
		return 1;
	}else{
		return 0;
	}
}

bool ffmpeg_data::open_audio_codec()
{
	AVDictionary *codec_opts=NULL;
	int ret = 0;
	if (!mAcodec->defaults &&
		!av_dict_get(codec_opts, "b", NULL, 0) &&
		!av_dict_get(codec_opts, "ab", NULL, 0))
		av_dict_set(&codec_opts, "b", "128k", 0);

	av_dict_set(&codec_opts, "threads", "auto", 0);

	if ((ret = avcodec_open2(mAudioCodecCtx, mAcodec, &codec_opts)) < 0) {
		av_log(NULL, AV_LOG_FATAL,
			"Error open video codec context.\n");
		return ret;
	}
	if (mAudioCodecCtx->bit_rate && mAudioCodecCtx->bit_rate < 1000){
		av_log(NULL, AV_LOG_WARNING, "The bitrate parameter is set too low."
			" It takes bits/s as argument, not kbits/s\n");
	}
	if (mAudioStream->codec != mAudioCodecCtx){
		ret = avcodec_copy_context(mAudioStream->codec, mAudioCodecCtx);
		if (ret < 0) {
			av_log(NULL, AV_LOG_FATAL,
				"Error initializing the output stream codec context.\n");
			return ret;
		}
		mAudioStream->codec->codec = mAudioCodecCtx->codec;
	}

	if (mAudioCodecCtx->nb_coded_side_data) {
		int i;

		mAudioStream->side_data = (AVPacketSideData*)av_realloc_array(NULL, mAudioCodecCtx->nb_coded_side_data,
			sizeof(*mAudioStream->side_data));
		if (!mAudioStream->side_data)
			return AVERROR(ENOMEM);

		for (i = 0; i < mAudioCodecCtx->nb_coded_side_data; i++) {
			const AVPacketSideData *sd_src = &mAudioCodecCtx->coded_side_data[i];
			AVPacketSideData *sd_dst = &mAudioStream->side_data[i];

			sd_dst->data =(uint8_t*) av_malloc(sd_src->size);
			if (!sd_dst->data)
				return AVERROR(ENOMEM);
			memcpy(sd_dst->data, sd_src->data, sd_src->size);
			sd_dst->size = sd_src->size;
			sd_dst->type = sd_src->type;
			mAudioStream->nb_side_data++;
		}
	}
	// copy timebase while removing common factors
	AVRational rational = { 0, 1 };
	mAudioStream->time_base = av_add_q(mAudioCodecCtx->time_base, rational);
}

AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
	AVFrame *picture=NULL;
	int ret=0;

	picture = av_frame_alloc();
	if (!picture)
		return NULL;

	picture->format = pix_fmt;
	picture->width = width;
	picture->height = height;

	/* allocate the buffers for the frame data */
	ret = av_frame_get_buffer(picture, 32);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate frame data.\n");
		av_frame_free(&picture);
		return NULL;
	}

	return picture;
}

bool ffmpeg_data::create_video_stream()
{
	//create video stream
	mVideoStream = avformat_new_stream(mOutput, NULL);
	mVideoStream->avg_frame_rate = mConfig.videoconfig.frame_rate;
	mVideoStream->r_frame_rate = mConfig.videoconfig.frame_rate;

	//init video codec
	mVcodec = avcodec_find_encoder((AVCodecID)mConfig.videoconfig.codecId);

	mVideoCodecCtx = avcodec_alloc_context3(mVcodec);

	mVideoCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	if (mOutput->oformat->flags & AVFMT_GLOBALHEADER)
		mVideoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	mVideoCodecCtx->width = mConfig.videoconfig.width;
	mVideoCodecCtx->height = mConfig.videoconfig.height;
	mVideoCodecCtx->framerate = mConfig.videoconfig.frame_rate;
	mVideoCodecCtx->pix_fmt = mConfig.videoconfig.pix_fmt;
	mVideoCodecCtx->time_base = mConfig.videoconfig.timebase;
	mVideoCodecCtx->sample_aspect_ratio = mConfig.videoconfig.frame_aspect_ratio;

	if (!pixel_fmt_support(mVcodec, mVideoCodecCtx->pix_fmt)){
		mVideoCodecCtx->pix_fmt = mVcodec->pix_fmts[0];
	}
	//open video codec
	open_video_codec();
	//sws init
	if (mVideoCodecCtx->pix_fmt != mConfig.videoconfig.pix_fmt){
		mSwscale = sws_getContext(mVideoCodecCtx->width, mVideoCodecCtx->height,
			mConfig.videoconfig.pix_fmt,
			mVideoCodecCtx->width, mVideoCodecCtx->height,
			mVideoCodecCtx->pix_fmt,
			SWS_BICUBIC, NULL, NULL, NULL);
		if (!mSwscale) {
			fprintf(stderr,
				"Could not initialize the conversion context\n");
			return false;
		}
		mVframe = alloc_picture(mVideoCodecCtx->pix_fmt, mVideoCodecCtx->width, mVideoCodecCtx->height);
		if (!mVframe) {
			fprintf(stderr, "Could not allocate temporary picture\n");
			return false;
		}
	}
	return true;
}

AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
	uint64_t channels,
	int sample_rate, int nb_samples)
{
	AVFrame *frame = av_frame_alloc();
	int ret=0;

	if (!frame) {
		fprintf(stderr, "Error allocating an audio frame\n");
		exit(1);
	}

	frame->format = sample_fmt;
	frame->channels = channels;
	frame->sample_rate = sample_rate;
	frame->nb_samples = nb_samples;

	if (nb_samples) {
		ret = av_frame_get_buffer(frame, 0);
		if (ret < 0) {
			fprintf(stderr, "Error allocating an audio buffer\n");
			av_frame_free(&frame);
			return NULL;
		}
	}

	return frame;
}

bool ffmpeg_data::create_audio_stream()
{	
	audioCodec_cfg_t *cfg = &mConfig.audioconfig;
	int ret = 0;
	//create video stream
	mVideoStream = avformat_new_stream(mOutput, NULL);
	//init audio codec
	mAcodec = avcodec_find_encoder((AVCodecID)mConfig.audioconfig.codecId);

	mAudioCodecCtx = avcodec_alloc_context3(mAcodec);
	mAudioCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
	mAudioCodecCtx->channels = cfg->channels;

	mAudioCodecCtx->sample_fmt = cfg->sample_fmt;
	mAudioCodecCtx->sample_rate = cfg->sample_rate;
	mAudioCodecCtx->time_base = cfg->timebase;
	if (!sample_fmt_support(mAcodec, mAudioCodecCtx->sample_fmt)){
		mAudioCodecCtx->sample_fmt = mAcodec->sample_fmts[0];
	}
	if (mOutput->oformat->flags & AVFMT_GLOBALHEADER)
		mAudioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	//open audio codec
	open_audio_codec();
	//swr init
	mAFrame_size = mAudioCodecCtx->frame_size ? mAudioCodecCtx->frame_size : 1024;
	if (mAudioCodecCtx->sample_fmt != cfg->sample_fmt){
		mAframe = alloc_audio_frame(mAudioCodecCtx->sample_fmt, mAudioCodecCtx->channels,
			mAudioCodecCtx->sample_rate, mAFrame_size);
		/* create resampler context */
		mAudioSwrCtx = swr_alloc();
		if (!mAudioSwrCtx) {
			fprintf(stderr, "Could not allocate resampler context\n");
			return false;
		}

		/* set options */
		av_opt_set_int(mAudioSwrCtx, "in_channel_count", mAudioCodecCtx->channels, 0);
		av_opt_set_int(mAudioSwrCtx, "in_sample_rate", mAudioCodecCtx->sample_rate, 0);
		av_opt_set_sample_fmt(mAudioSwrCtx, "in_sample_fmt", cfg->sample_fmt, 0);
		av_opt_set_int(mAudioSwrCtx, "out_channel_count", mAudioCodecCtx->channels, 0);
		av_opt_set_int(mAudioSwrCtx, "out_sample_rate", mAudioCodecCtx->sample_rate, 0);
		av_opt_set_sample_fmt(mAudioSwrCtx, "out_sample_fmt", mAudioCodecCtx->sample_fmt, 0);

		/* initialize the resampling context */
		if ((ret = swr_init(mAudioSwrCtx)) < 0) {
			fprintf(stderr, "Failed to initialize the resampling context\n");
			return -1;
		}
	}
}

bool ffmpeg_data::init_streams()
{
	if (!create_video_stream())
		goto fail;
	if (!create_audio_stream())
		goto fail;
	return true;
fail:
	return false;
}

bool ffmpeg_data::open_output_file()
{
	char errstr[AV_ERROR_MAX_STRING_SIZE]="";
	int ret = 0;
	if (!(mOutput->oformat->flags & AVFMT_NOFILE)) {
		/* open the file */
		if ((ret = avio_open2(&mOutput->pb, mConfig.url, AVIO_FLAG_WRITE,
			NULL,NULL)) < 0) {
			av_log(NULL, AV_LOG_WARNING, "fail to open the %s", mConfig.url);
			goto fail;
		}
	}
	ret = avformat_write_header(mOutput, NULL);
	if (ret < 0) {
		av_log(NULL, AV_LOG_WARNING, "Error opening '%s': %s",
			mConfig.url, av_make_error_string(errstr, AV_ERROR_MAX_STRING_SIZE,ret));
		goto fail;
	}

	return true;
fail:
	return false;
}

bool ffmpeg_data::ffmpeg_data_init(ffmpeg_cfg *cfg)
{
	bool is_rtmp = false;

	if (!cfg->url || !*cfg->url)
		return false;
	mConfig = *cfg;

	is_rtmp = (astrcmpi_n(cfg->url, "rtmp://", 7) == 0);

	AVOutputFormat *output_format = av_guess_format(
		is_rtmp ? "flv" : cfg->format_name,
		cfg->url,
		is_rtmp ? NULL : cfg->format_mime_type);

	if (output_format == NULL) {
		av_log(NULL, AV_LOG_WARNING, "Couldn't find matching output format with "
			" parameters: name=%s, url=%s, mime=%s",
			safe_str(is_rtmp ? "flv" : cfg->format_name),
			safe_str(cfg->url),
			safe_str(is_rtmp ? NULL : cfg->format_mime_type));
		goto fail;
	}
	avformat_alloc_output_context2(&mOutput, output_format,
		NULL, cfg->url);

	if (!mOutput) {
		av_log(NULL,AV_LOG_WARNING, "Couldn't create avformat context");
		goto fail;
	}

	if (!init_streams())
		goto fail;
	if (!open_output_file())
		goto fail;

	av_dump_format(mOutput, 0, NULL, 1);

	mInitialized = true;
	return true;

fail:
	av_log(NULL, AV_LOG_WARNING, "ffmpeg_data_init failed");
	return false;

}

int ffmpeg_data::encode_audio(AVPacket *packet, AVFrame *frame, int *got_packet)
{
	uint64_t pts_us = 0;

	av_init_packet(packet);
	packet->data = NULL;
	packet->size = 0;

	AVRational default_timebase = { 1, AV_TIME_BASE };
	pts_us = av_rescale_q(frame->pts, mAudioCodecCtx->time_base, default_timebase);

	if (avcodec_encode_audio2(mAudioCodecCtx, packet, frame, got_packet) < 0) {
		av_log(NULL, AV_LOG_FATAL, "Audio encoding failed (avcodec_encode_audio2)\n");
		return -1;
	}

	if (got_packet){
		av_packet_rescale_ts(packet, mAudioCodecCtx->time_base, mAudioStream->time_base);
	}
	return 0;
}

int ffmpeg_data::encode_video(AVPacket *packet, AVFrame *frame, int *got_packet)
{
	AVFrame *in_picture = frame;
	int ret = NULL;
	double duration = 1;
	int debug = 0;

	av_init_packet(packet);
	packet->data = NULL;
	packet->size = 0;

	duration = FFMIN(duration, 1 / (av_q2d(mVideoCodecCtx->framerate) * av_q2d(mVideoCodecCtx->time_base)));
	in_picture->pkt_duration = duration;
	in_picture->pict_type = AV_PICTURE_TYPE_NONE;
	//printf("in_picture->pts = %d\n", in_picture->pts);

	if (in_picture->pkt_duration == 0) in_picture->pkt_duration = 1;

	ret = avcodec_encode_video2(mVideoCodecCtx,packet, in_picture, got_packet);
	if (ret < 0) {
		av_log(NULL, AV_LOG_FATAL, "Video encoding failed\n");
		return -1;
	}

	if (0 == packet->duration) packet->duration = 1;

	if (got_packet) {
		av_packet_rescale_ts(packet, mVideoCodecCtx->time_base, mVideoStream->time_base);
	}
	return 0;
}

void ffmpeg_data::close_video()
{
	avcodec_close(mVideoCodecCtx);
	avcodec_free_context(&mVideoCodecCtx);

	mVideoCodecCtx = NULL;
	if (mSwscale)
	{
		sws_freeContext(mSwscale);
		mSwscale = NULL;
	}
	if (mVframe){
		av_frame_free(&mVframe);
		mVframe = NULL;
	}
}
void ffmpeg_data::close_audio()
{
	avcodec_close(mAudioCodecCtx);
	avcodec_free_context(&mAudioCodecCtx);
	mAudioCodecCtx = NULL;

	if (mAudioSwrCtx)
	{
		swr_free(&mAudioSwrCtx);
		mAudioSwrCtx = NULL;
	}
	if (mAframe){
		av_frame_free(&mAframe);
		mAframe = NULL;
	}
}

void ffmpeg_data::free()
{
	if (mInitialized)
		av_write_trailer(mOutput);

	if (mVideoStream)
		close_video();
	if (mAudioStream)
		close_audio();

	if (mOutput) {
		if ((mOutput->oformat->flags & AVFMT_NOFILE) == 0)
			avio_close(mOutput->pb);

		avformat_free_context(mOutput);
	}
	reset_ffmpeg_data();
}


//output 
int ffmpeg_output::audio_frame(AVFrame *frame)
{
	AVPacket packet = { 0 };
	int ret = 0, got_packet;

	av_init_packet(&packet);

	ret = mff_data.encode_audio(&packet, frame, &got_packet);
	if (ret < 0){
		return ret;
	}
	if (got_packet){
		push_back_packet(&packet);
	}
}
int ffmpeg_output::video_frame(AVFrame *frame)
{
	AVPacket packet = { 0 };
	int ret = 0, got_packet=0;

	av_init_packet(&packet);

	if (!mStart_timestamp)
		mStart_timestamp = frame->pts;

	ret = mff_data.encode_video(&packet, frame, &got_packet);
	if (ret < 0){
		return ret;
	}
	if (got_packet){
		push_back_packet(&packet);
	}
}
