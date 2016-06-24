#include "ffplay.h"

void setMasterClock(OutputFile *outFile, int64_t pts)
{
	outFile->master_pts = pts;
	outFile->master_time = av_gettime_relative();
	outFile->master_inited = 1;
}

static void choose_encoder(OptionsContext *o, AVFormatContext *s, OutputStream *ost)
{
	enum AVCodecID codec_id = AV_CODEC_ID_NONE;
	if (AVMEDIA_TYPE_VIDEO != ost->st->codec->codec_type)
		codec_id = o->a_codec_id;
	else
		codec_id= o->v_codec_id;

	if (AV_CODEC_ID_NONE == codec_id) {
		ost->st->codec->codec_id = av_guess_codec(s->oformat, NULL, s->filename,
			NULL, ost->st->codec->codec_type);
		ost->enc = avcodec_find_encoder(ost->st->codec->codec_id);
	}
	else {
		ost->enc = avcodec_find_encoder(codec_id);
		ost->st->codec->codec_id = ost->enc->id;
	}
}
OutputStream *new_output_stream(OptionsContext *o, OutputFile * outFile, enum AVMediaType type)
{
	OutputStream *ost;
	AVFormatContext *oc = outFile->ctx;
	AVStream *st = avformat_new_stream(oc, NULL);
	int idx = oc->nb_streams - 1, ret = 0;
	char *codec_tag = NULL;
	double qscale = -1;
	int i;

	if (!st) {
		av_log(NULL, AV_LOG_FATAL, "Could not alloc stream.\n");
		exit_program(1);
	}

	GROW_ARRAY(outFile->output_streams, outFile->nb_output_streams);
	if (!(ost = av_mallocz(sizeof(*ost))))
		exit_program(1);
	outFile->output_streams[outFile->nb_output_streams - 1] = ost;

	ost->file = outFile;
	ost->index = idx;
	ost->st = st;
	ost->type = type;
	ost->encoding_needed = 1;
	st->codec->codec_type = type;
	choose_encoder(o, oc, ost);

	ost->enc_ctx = avcodec_alloc_context3(ost->enc);//st->codec;
	if (!ost->enc_ctx) {
		av_log(NULL, AV_LOG_ERROR, "Error allocating the encoding context.\n");
		exit_program(1);
	}
	ost->enc_ctx->codec_type = type;

	if (ost->enc) {
		AVIOContext *s = NULL;
		char *arg = NULL;

		//ost->encoder_opts = filter_codec_opts(o->codec_opts, ost->enc->id, oc, st, ost->enc);
	}
	else {
		ost->codec_opts = filter_codec_opts(o->codec_opts, AV_CODEC_ID_NONE, oc, st, NULL);
	}

	ost->max_frames = INT64_MAX;

	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		ost->enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	av_dict_copy(&ost->sws_dict, o->sws_dict, 0);

	av_dict_copy(&ost->swr_opts, o->swr_opts, 0);
	if (ost->enc && av_get_exact_bits_per_sample(ost->enc->id) == 24)
		av_dict_set(&ost->swr_opts, "output_sample_bits", "24", 0);

	av_dict_copy(&ost->resample_opts, o->resample_opts, 0);
	ost->last_mux_dts = AV_NOPTS_VALUE;

	return ost;
}

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
	uint64_t channels,
	int sample_rate, int nb_samples)
{
	AVFrame *frame = av_frame_alloc();
	int ret;

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
			exit(1);
		}
	}

	return frame;
}
OutputStream *new_audio_stream(OptionsContext *o, OutputFile * outFile)
{
	int n;
	AVStream *st;
	OutputStream *ost;
	AVCodecContext *audio_enc;
	AVFormatContext *oc = outFile->ctx;
	int ret = 0;
	int nb_samples = 1024;

	ost = new_output_stream(o, outFile, AVMEDIA_TYPE_AUDIO);
	st = ost->st;
	ost->inputFrame = &(outFile->is->audioQueue);
	ost->tb_decinput = o->a_dectb;
	ost->tb_pktinput = o->a_pkttb;
	audio_enc = ost->enc_ctx;
	audio_enc->codec_type = AVMEDIA_TYPE_AUDIO;

	//avcodec_get_context_defaults3(audio_enc, ost->enc);

	enum AVSampleFormat sample_fmt = AV_SAMPLE_FMT_S16;

	audio_enc->channels = o->audio_channels;

	sample_fmt = o->sample_fmt;
	if (AV_SAMPLE_FMT_NONE == sample_fmt)
		sample_fmt = AV_SAMPLE_FMT_S16;
	audio_enc->sample_fmt = sample_fmt;
	audio_enc->bit_rate = o->a_bitrate;
	audio_enc->sample_rate = o->sample_rate;
	audio_enc->time_base.den = o->a_dectb.den;
	audio_enc->time_base.num = o->a_dectb.num;
	ost->src_sample_fmt = ost->dst_sample_fmt = audio_enc->sample_fmt;

	if (!sample_fmt_support(ost->enc, audio_enc->sample_fmt)){
		ost->a_tempFrame = alloc_audio_frame(ost->enc->sample_fmts[0], audio_enc->channels,
			audio_enc->sample_rate, nb_samples);
		/* create resampler context */
		ost->audioswr_ctx = swr_alloc();
		if (!ost->audioswr_ctx) {
			fprintf(stderr, "Could not allocate resampler context\n");
			exit(1);
		}

		/* set options */
		av_opt_set_int(ost->audioswr_ctx, "in_channel_count", audio_enc->channels, 0);
		av_opt_set_int(ost->audioswr_ctx, "in_sample_rate", audio_enc->sample_rate, 0);
		av_opt_set_sample_fmt(ost->audioswr_ctx, "in_sample_fmt", audio_enc->sample_fmt, 0);
		av_opt_set_int(ost->audioswr_ctx, "out_channel_count", audio_enc->channels, 0);
		av_opt_set_int(ost->audioswr_ctx, "out_sample_rate", audio_enc->sample_rate, 0);
		av_opt_set_sample_fmt(ost->audioswr_ctx, "out_sample_fmt", ost->enc->sample_fmts[0], 0);
		audio_enc->sample_fmt = ost->enc->sample_fmts[0];
		ost->dst_sample_fmt = audio_enc->sample_fmt;

		/* initialize the resampling context */
		if ((ret = swr_init(ost->audioswr_ctx)) < 0) {
			fprintf(stderr, "Failed to initialize the resampling context\n");
			exit(1);
		}
	}

	return ost;
}


static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
	AVFrame *picture;
	int ret;

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
		exit(1);
	}

	return picture;
}
OutputStream *new_video_stream(OptionsContext *o, OutputFile * outFile)
{
	AVStream *st;
	OutputStream *ost;
	AVCodecContext *video_enc;
	AVFormatContext *oc = outFile->ctx;

	ost = new_output_stream(o, outFile, AVMEDIA_TYPE_VIDEO);
	st = ost->st;
	ost->inputFrame = &(outFile->is->videoQueue);
	ost->tb_decinput = o->v_dectb;
	ost->tb_pktinput = o->v_pkttb;
	st->avg_frame_rate = o->frame_rate;
	st->r_frame_rate = o->frame_rate;
	video_enc = ost->enc_ctx;
	//avcodec_get_context_defaults3(video_enc, ost->enc);

	if (!o->width) o->width = 640;
	video_enc->width = o->width;
	if (!o->height) o->height = 480;
	video_enc->height = o->height;

	//if (!o->frame_rate.num || !o->frame_rate.den) o->frame_rate = (AVRational){ 30000, 1001 };
	ost->frame_rate = o->frame_rate;
	video_enc->framerate = ost->frame_rate;

	//if (!o->frame_aspect_ratio.num || !o->frame_aspect_ratio.den) 
	//	o->frame_aspect_ratio = (AVRational){ 30000, 1001 };
	ost->frame_aspect_ratio = o->frame_aspect_ratio;
	//video_enc->sample_aspect_ratio = ost->frame_aspect_ratio;

	video_enc->width = o->width;
	video_enc->height = o->height;

	video_enc->pix_fmt = o->pix_fmt;

	st->sample_aspect_ratio = video_enc->sample_aspect_ratio;

	video_enc->time_base.den = o->v_dectb.den;
	video_enc->time_base.num = o->v_dectb.num;
	//video_enc->bit_rate = o->v_bitrate;
	//video_enc->gop_size = -1; /* emit one intra frame every twelve frames at most */

	ost->src_pix_fmt = ost->dst_pix_fmt = video_enc->pix_fmt;
	if (!pixel_fmt_support(ost->enc, video_enc->pix_fmt)){
		ost->videosws_ctx = sws_getContext(video_enc->width, video_enc->height,
			video_enc->pix_fmt,
			video_enc->width, video_enc->height,
			ost->enc->pix_fmts[0],
			SCALE_FLAGS, NULL, NULL, NULL);
		if (!ost->videosws_ctx) {
			fprintf(stderr,
				"Could not initialize the conversion context\n");
			exit(1);
		}
		video_enc->pix_fmt = ost->enc->pix_fmts[0];
		ost->dst_pix_fmt = video_enc->pix_fmt;
		ost->v_tempFrame = alloc_picture(ost->dst_pix_fmt, video_enc->width, video_enc->height);
		if (!ost->v_tempFrame) {
			fprintf(stderr, "Could not allocate temporary picture\n");
			exit(1);
		}
	}


	return ost;
}

static int output_interrupt_cb(void *ctx)
{
	OutputFile *outFile = (OutputFile*)ctx;

	return outFile->abort;
}

int open_output_file(OptionsContext *o, const char *filename, VideoState *is)
{
	AVFormatContext *oc;
	int i, j, err;
	AVOutputFormat *file_oformat;
	OutputFile *of;
	OutputStream *ost;
	AVDictionary *unused_opts = NULL;
	AVDictionaryEntry *e = NULL;


	if (o->stop_time != INT64_MAX && o->recording_time != INT64_MAX) {
		o->stop_time = INT64_MAX;
		av_log(NULL, AV_LOG_WARNING, "-t and -to cannot be used together; using -t.\n");
	}

	if (o->stop_time != INT64_MAX && o->recording_time == INT64_MAX) {
		int64_t start_time = o->start_time == AV_NOPTS_VALUE ? 0 : o->start_time;
		if (o->stop_time <= start_time) {
			av_log(NULL, AV_LOG_ERROR, "-to value smaller than -ss; aborting.\n");
			exit_program(1);
		}
		else {
			o->recording_time = o->stop_time - start_time;
		}
	}

	GROW_ARRAY(is->output_files, is->nb_output_files);
	of = av_mallocz(sizeof(*of));
	if (!of)
		exit_program(1);
	is->output_files[is->nb_output_files - 1] = of;

	of->is = is;
	of->recording_time = o->recording_time;
	of->start_time = o->start_time;
	of->limit_filesize = o->limit_filesize;
	of->shortest = o->shortest;
	of->ts_offset = AV_NOPTS_VALUE;
	if (o->format_opts)
	  av_dict_copy(&of->opts, o->format_opts, 0);

	err = avformat_alloc_output_context2(&oc, NULL, o->format, filename);
	if (!oc) {
		print_error(filename, err);
		exit_program(1);
	}

	of->filename = filename;
	of->ctx = oc;
	//if (o->recording_time != INT64_MAX)
	//	oc->duration = o->recording_time;

	file_oformat = oc->oformat;
	oc->interrupt_callback.callback = output_interrupt_cb;
	oc->interrupt_callback.opaque = of;

	new_video_stream(o, of);
	new_audio_stream(o, of);
#if 1
	if (!(oc->oformat->flags & AVFMT_NOFILE)) {
		/* test if it already exists to avoid losing precious files */
		//assert_file_overwrite(filename);

		/* open the file */
		if ((err = avio_open2(&oc->pb, filename, AVIO_FLAG_WRITE,
			&oc->interrupt_callback,
			&of->opts)) < 0) {
			print_error(filename, err);
			exit_program(1);

		}

	}
#endif

	oc->max_delay = (int)(o->mux_max_delay * AV_TIME_BASE);
	return 0;
}


static int init_output_stream(OutputStream *ost, char *error, int error_len)
{
	int ret = 0;
	if (ost->encoding_needed) {
		AVCodec      *codec = ost->enc;

		if (!av_dict_get(ost->codec_opts, "threads", NULL, 0))
			av_dict_set(&ost->codec_opts, "threads", "auto", 0);

		if (ost->enc->type == AVMEDIA_TYPE_AUDIO &&
			!codec->defaults &&
			!av_dict_get(ost->codec_opts, "b", NULL, 0) &&
			!av_dict_get(ost->codec_opts, "ab", NULL, 0))
			av_dict_set(&ost->codec_opts, "b", "128000", 0);
		if (ost->enc->type == AVMEDIA_TYPE_VIDEO){
			av_opt_set(ost->enc_ctx->priv_data, "tune", "zerolatency", 0);
			av_dict_set(&ost->codec_opts, "b", "500k", 0);
			//av_dict_set(&ost->encoder_opts, "profile", "baseline", 0);
		}
		if ((ret = avcodec_open2(ost->enc_ctx, codec, &ost->codec_opts)) < 0) {
			//if (ret == AVERROR_EXPERIMENTAL)
				//abort_codec_experimental(codec, 1);
			//snprintf(error, error_len,
			//	"Error while opening encoder for output stream %d - "
			//	"maybe incorrect parameters such as bit_rate, rate, width or height",
			//	ost->index);
			return ret;
		}
		//if (ost->encoder_opts)
		     //assert_avoptions(ost->encoder_opts);
#if 0
		if (ost->enc->type == AVMEDIA_TYPE_AUDIO &&
			!(ost->enc->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
			av_buffersink_set_frame_size(ost->filter->filter,
			ost->enc_ctx->frame_size);
#endif
		if (ost->enc_ctx->bit_rate && ost->enc_ctx->bit_rate < 1000)
			av_log(NULL, AV_LOG_WARNING, "The bitrate parameter is set too low."
			" It takes bits/s as argument, not kbits/s\n");

		if (ost->st->codec != ost->enc_ctx){
			ret = avcodec_copy_context(ost->st->codec, ost->enc_ctx);
			if (ret < 0) {
				av_log(NULL, AV_LOG_FATAL,
					"Error initializing the output stream codec context.\n");
				exit_program(1);
			}
		}
#if 1
		if (ost->enc_ctx->nb_coded_side_data) {
			int i;

			ost->st->side_data = av_realloc_array(NULL, ost->enc_ctx->nb_coded_side_data,
				sizeof(*ost->st->side_data));
			if (!ost->st->side_data)
				return AVERROR(ENOMEM);

			for (i = 0; i < ost->enc_ctx->nb_coded_side_data; i++) {
				const AVPacketSideData *sd_src = &ost->enc_ctx->coded_side_data[i];
				AVPacketSideData *sd_dst = &ost->st->side_data[i];

				sd_dst->data = av_malloc(sd_src->size);
				if (!sd_dst->data)
					return AVERROR(ENOMEM);
				memcpy(sd_dst->data, sd_src->data, sd_src->size);
				sd_dst->size = sd_src->size;
				sd_dst->type = sd_src->type;
				ost->st->nb_side_data++;
			}
		}
#endif
		// copy timebase while removing common factors
		ost->st->time_base = av_add_q(ost->enc_ctx->time_base, (AVRational){ 0, 1 });
		ost->st->codec->codec = ost->enc_ctx->codec;
	}
	else {
		ret = av_opt_set_dict(ost->enc_ctx, &ost->codec_opts);
		if (ret < 0) {
			av_log(NULL, AV_LOG_FATAL,
				"Error setting up codec context options.\n");
			return ret;
		}
		// copy timebase while removing common factors
		ost->st->time_base = av_add_q(ost->st->codec->time_base, (AVRational){ 0, 1 });
	}

	return ret;
}

static int transcode_init(VideoState *is)
{
	int ret = 0, i, j, k;
	AVFormatContext *oc;
	OutputStream *ost;
	char error[1024] = { 0 };
	OutputFile *outFile = is->output_files[0];
	int  err;

	/* for each output stream, we compute the right encoding parameters */
	for (i = 0; i < outFile->nb_output_streams; i++) {
		AVCodecContext *enc_ctx;
		ost = outFile->output_streams[i];
		oc = outFile->ctx;
	}

	/* open each encoder */
	for (i = 0; i < outFile->nb_output_streams; i++) {
		ret = init_output_stream(outFile->output_streams[i], error, sizeof(error));
		if (ret < 0)
			goto dump_format;
	}

	avformat_write_header(outFile->ctx, &outFile->opts);

dump_format:
	/* dump the file output parameters - cannot be done before in case
	of stream copy */
	av_dump_format(outFile->ctx, 0, outFile->ctx->filename, 1);
	return 0;
}

static int write_frame(AVFormatContext *fmt_ctx, AVPacket *pkt,OutputStream *ost)
{
	/* rescale output packet timestamp values from codec to stream timebase */
	pkt->stream_index = ost->st->index;
	int err = 0;

	/* Write the compressed frame to the media file. */
	//log_packet(fmt_ctx, pkt);
	//return av_write_frame(fmt_ctx, pkt);
	int64_t begin = av_gettime_relative();

	err= av_interleaved_write_frame(fmt_ctx, pkt);
	int duration = av_gettime_relative() - begin;
	if (err < 0){
		printf("write_frame fail:%d,duration =%d\n", err, duration);
	}
	return err;
}


int writeMediaFrame(AVFrame *frame, OutputStream*ost)
{
	int ret = 0;
	OutputFile    *outFile = ost->file;
	AVCodecContext *enc = ost->enc_ctx;
	double float_pts = AV_NOPTS_VALUE;

	if (frame->pts != AV_NOPTS_VALUE) {
		int64_t start_time = (outFile->start_time == AV_NOPTS_VALUE) ? 0 : outFile->start_time;
		AVRational tb = enc->time_base;
		int extra_bits = av_clip(29 - av_log2(tb.den), 0, 16);


		tb.den <<= extra_bits;
		float_pts =
			av_rescale_q(frame->pts, ost->tb_decinput, tb) -
			av_rescale_q(start_time, AV_TIME_BASE_Q, tb);
		float_pts /= 1 << extra_bits;
		// avoid exact midoints to reduce the chance of rounding differences, this can be removed in case the fps code is changed to work with integers
		float_pts += FFSIGN(float_pts) * 1.0 / (1 << 17);

		frame->pts =
			av_rescale_q(frame->pts, ost->tb_decinput, enc->time_base) -
			av_rescale_q(start_time, AV_TIME_BASE_Q, enc->time_base);
	}

	switch (ost->type){
	case AVMEDIA_TYPE_AUDIO:
		if (!(enc->codec->capabilities & AV_CODEC_CAP_PARAM_CHANGE) &&
			enc->channels != av_frame_get_channels(frame)) {
			av_log(NULL, AV_LOG_ERROR,
				"Audio filter graph output is not normalized and encoder does not support parameter changes\n");
			break;
		}
		if (ost->dst_sample_fmt != ost->src_sample_fmt){
			int dst_nb_samples = av_rescale_rnd(swr_get_delay(ost->audioswr_ctx, ost->enc_ctx->sample_rate) + frame->nb_samples,
				ost->enc_ctx->sample_rate, ost->enc_ctx->sample_rate, AV_ROUND_UP);
			//av_assert0(dst_nb_samples == frame->nb_samples);
			ret = av_frame_make_writable(ost->a_tempFrame);
			if (ret < 0)
				exit(1);
			ret = swr_convert(ost->audioswr_ctx,
				ost->a_tempFrame->data, dst_nb_samples,
				(const uint8_t **)frame->data, frame->nb_samples);
			if (ret < 0) {
				fprintf(stderr, "Error while converting\n");
				exit(1);
			}
			ost->a_tempFrame->pts = frame->pts;
			do_audio_out(outFile->ctx, ost, ost->a_tempFrame);
		}
		else{
			do_audio_out(outFile->ctx, ost, frame);
		}
		break;
	case AVMEDIA_TYPE_VIDEO:
		if (!ost->frame_aspect_ratio.num)
			enc->sample_aspect_ratio = frame->sample_aspect_ratio;

		if (1) {
			/*av_log(NULL, AV_LOG_INFO, "filter -> pts:%s pts_time:%s exact:%f time_base:%d/%d\n",
				av_ts2str(frame->pts), av_ts2timestr(frame->pts, &enc->time_base),
				float_pts,
				enc->time_base.num, enc->time_base.den);*/
		}
		if (frame->format != ost->dst_pix_fmt){
			ret = av_frame_make_writable(ost->v_tempFrame);
			if (ret < 0)
				exit(1);
			sws_scale(ost->videosws_ctx,
				(const uint8_t * const *)frame->data, frame->linesize,
				0, ost->enc_ctx->height, ost->v_tempFrame->data, ost->v_tempFrame->linesize);
			ost->v_tempFrame->pts = frame->pts;
			do_video_out(outFile->ctx, ost, frame,float_pts);
		}
		else{
			do_video_out(outFile->ctx, ost, frame, float_pts);
		}	
		break;
	default:
		// TODO support subtitle filters
		av_assert0(0);
	}

	return 0;
}

AVFrame * get_Frame(OutputStream *ost)
{
	AVFrame *av_frame = NULL;
	Frame * frame = NULL;
#if 0
	if (tframe_queue_nb_remaining(ost->inputFrame) == 0){
		av_frame = NULL;
	}
	else{
#endif
		frame = tframe_queue_peek_readable(ost->inputFrame);
		if (frame)  av_frame = frame->frame;
	//}
	return av_frame;
}	

OutputStream *chooseStream(OutputFile *outFile)
{
	OutputStream *outputStream = NULL;

	//
	if (AV_NOPTS_VALUE == outFile->ts_offset)
	{
		for (int i = 0; i < outFile->nb_output_streams; i++){
			outputStream = outFile->output_streams[i];
			if (AVMEDIA_TYPE_AUDIO == outputStream->type){
				return outputStream;
			}
		}
	}

	outputStream = NULL;

	int64_t next_pts = INT64_MAX;
	for (int i = 0; i < outFile->nb_output_streams; i++){
		OutputStream *temp = outFile->output_streams[i];
		if (temp->next_pts <= next_pts){
			outputStream = temp;
			next_pts = temp->next_pts;
		}
	}
#if 0
	if (outputStream &&(tframe_queue_nb_remaining(outputStream->inputFrame) == 0)){
		for (int i = 0; i < outFile->nb_output_streams; i++){
			OutputStream *temp = outFile->output_streams[i];
			if (tframe_queue_nb_remaining(temp->inputFrame)){
				outputStream = temp;
				break;
			}
		}
	}
#endif
	return outputStream;
}

void waitForFrameReady(VideoState*is)
{
	SDL_LockMutex(is->transcode_mutex);
	int a_remain = tframe_queue_nb_remaining(&is->audioQueue);
	int v_remain = tframe_queue_nb_remaining(&is->videoQueue);
	if (!a_remain && !v_remain){
		SDL_CondWaitTimeout(is->transcode_singal, is->transcode_mutex, 10);
	}
	SDL_UnlockMutex(is->transcode_mutex);
}

void transcode_step(VideoState *is)
{
	OutputFile * outFile = is->output_files[0];
	OutputStream *ost = chooseStream(outFile);
	AVFrame *frame = NULL;

	if (!ost) return;

	frame = get_Frame(ost);

	if (frame){
		if (AV_NOPTS_VALUE == outFile->ts_offset){
			outFile->ts_offset = av_rescale_q(frame->pkt_pts, ost->tb_pktinput, AV_TIME_BASE_Q);
		}

		if (AV_NOPTS_VALUE == frame->pts){
			frame->pts = av_rescale_q(frame->pkt_pts, ost->tb_pktinput,ost->tb_decinput);
		}

		if (AV_NOPTS_VALUE != frame->pkt_dts){
			frame->pkt_dts -= av_rescale_q(outFile->ts_offset, AV_TIME_BASE_Q, ost->tb_pktinput);
		}

		if (AV_NOPTS_VALUE != frame->pkt_pts){
			frame->pkt_pts -= av_rescale_q(outFile->ts_offset, AV_TIME_BASE_Q, ost->tb_pktinput);
		}

		if (AV_NOPTS_VALUE != frame->pts){

			frame->pts -= av_rescale_q(outFile->ts_offset, AV_TIME_BASE_Q, ost->tb_decinput);
		}

		writeMediaFrame(frame, ost);
		tframe_queue_next(ost->inputFrame);
	}
}

void cleanup(VideoState*is)
{
	for (int i = 0; i < is->nb_output_files; i++) {
		OutputFile *of = is->output_files[i];
		AVFormatContext *s;
		if (!of)
			continue;

		s = of->ctx;
		if (s && s->oformat && !(s->oformat->flags & AVFMT_NOFILE))
			avio_closep(&s->pb);

		for (int j = 0; j < of->nb_output_streams; j++) {
			OutputStream *ost = of->output_streams[j];

			if (!ost)
				continue;

			av_frame_free(&ost->filtered_frame);
			av_frame_free(&ost->last_frame);

			av_parser_close(ost->parser);

			av_freep(&ost->audio_channels_map);
			ost->audio_channels_mapped = 0;

			av_dict_free(&ost->sws_dict);

			av_frame_free(&ost->a_tempFrame);
			av_frame_free(&ost->v_tempFrame);

			av_freep(&(of->output_streams[j]));
		}

		avformat_free_context(s);
		av_dict_free(&of->opts);

		av_freep(&(of->output_streams));
		av_freep(&(is->output_files[i]));
	}
	av_freep(&(is->output_files));
	is->nb_output_files = 0;
}

 int transcode_thread(void *arg)
{
	VideoState *is = arg;
	OptionsContext *o = is->optctx;
	//char *filename = "D:/test.flv";
	//char *filename = "rtmp://10.128.164.55:1935/live/stream_test3";
	//char *filename = "rtmp://10.0.76.118/mylive/1q2w3e4r";
	char *filename = "rtmp://10.128.164.55:1990/live/stream_test5";
	open_output_file(o, filename, is);
	transcode_init(is);
	is->btranscode = 1;
	int index = 0;
	while (!is->btranscode_exit)
	{
		waitForFrameReady(is);
		transcode_step(is);
	}

the_end:
	/* write the trailer if needed and close file */
	for (int i = 0; i < is->nb_output_files; i++) {
		OutputFile * outFile = is->output_files[i];
		AVFormatContext *os = outFile->ctx;
		av_write_trailer(os);
		/* close each encoder */
		for (int j = 0; j < outFile->nb_output_streams; j++) {
			OutputStream *ost = outFile->output_streams[j];
			if (ost->encoding_needed) {
				av_freep(&ost->enc_ctx->stats_in);
			}
			av_dict_free(&ost->codec_opts);
			av_dict_free(&ost->sws_dict);
			av_dict_free(&ost->swr_opts);
			av_dict_free(&ost->resample_opts);
			av_dict_free(&ost->format_opts);
		}
	}
	cleanup(is);
	return 0;
}

 void transcode_abort(VideoState *is)
 {
	 is->btranscode = 0;
	 is->btranscode_exit = 1;
	 SDL_CondSignal(is->transcode_singal);
	 av_usleep(1000);
	 tframe_abort(&is->audioQueue);
	 tframe_abort(&is->videoQueue);
	 SDL_WaitThread(is->transcode_tid, NULL);
 }
