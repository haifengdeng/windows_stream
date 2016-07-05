#include "input.h"

void ff_demuxer::stream_component_close(int stream_index)
{
	if (stream_index < 0 || stream_index >= mFormat_context->nb_streams)
		return;

	AVCodecContext *avctx = mFormat_context->streams[stream_index]->codec;

	switch (avctx->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		mAudio_decoder->decoder_abort();
		break;
	case AVMEDIA_TYPE_VIDEO:
		mVideo_decoder->decoder_abort();
		break;
	default:
		break;
	}

	mFormat_context->streams[stream_index]->discard = AVDISCARD_ALL;
	avcodec_close(avctx);
	switch (avctx->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		mAudio_st_index = -1;
		break;
	case AVMEDIA_TYPE_VIDEO:
		mVideo_st_index = -1;
		break;
	default:
		break;
	}
}

void ff_demuxer::demuxer_close()
{
	mAbort = 1;
	/* XXX: use a special url_shutdown call to abort parse cleanly */

	mDemuxer_thread->join();

	delete mDemuxer_thread;
	mDemuxer_thread = NULL;
	/* close each stream */
	if (mAudio_st_index >= 0)
		stream_component_close(mAudio_st_index);
	if (mVideo_st_index >= 0)
		stream_component_close(mVideo_st_index);

	//avformat_close_input(&mFormat_context);
	//av_freep(&mFormat_context);
	av_freep(&mInput);
	av_freep(&mInput_format);
}

int ff_demuxer::stream_component_open(int stream_index)
{
	AVCodecContext *avctx=NULL;
	AVCodec *codec=NULL;
	const char *forced_codec_name = NULL;
	AVDictionary *opts=NULL;
	AVDictionaryEntry *t = NULL;
	int sample_rate=0, nb_channels=0;
	int64_t channel_layout=0;
	int ret = 0;
	int stream_lowres = -1;

	if (stream_index < 0 || stream_index >= mFormat_context->nb_streams)
		return -1;

	avctx = mFormat_context->streams[stream_index]->codec;

	codec = avcodec_find_decoder(avctx->codec_id);

	av_codec_set_pkt_timebase(avctx, std_tb_us);

	switch (avctx->codec_type){
	case AVMEDIA_TYPE_AUDIO: 
		mAudio_st_index = stream_index;
		mAudioStream_tb = mFormat_context->streams[stream_index]->time_base;
		break;
	case AVMEDIA_TYPE_VIDEO: 
		mVideo_st_index = stream_index; 
		mVideoStream_tb = mFormat_context->streams[stream_index]->time_base;
		break;
	}

	if (!codec) {
		av_log(NULL, AV_LOG_WARNING,
			"No codec could be found with id %d\n", avctx->codec_id);
		return -1;
	}

	avctx->codec_id = codec->id;
	stream_lowres = av_codec_get_max_lowres(codec);
	av_log(avctx, AV_LOG_WARNING, "The maximum value for lowres supported by the decoder is %d\n",
			av_codec_get_max_lowres(codec));
	av_codec_set_lowres(avctx, stream_lowres);

#if FF_API_EMU_EDGE
	if (stream_lowres) avctx->flags |= CODEC_FLAG_EMU_EDGE;
#endif

	avctx->flags2 |= AV_CODEC_FLAG2_FAST;
#if FF_API_EMU_EDGE
	if (codec->capabilities & AV_CODEC_CAP_DR1)
		avctx->flags |= CODEC_FLAG_EMU_EDGE;
#endif

	if (!av_dict_get(opts, "threads", NULL, 0))
		av_dict_set(&opts, "threads", "auto", 0);
	if (stream_lowres)
		av_dict_set_int(&opts, "lowres", stream_lowres, 0);
	if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
		av_dict_set(&opts, "refcounted_frames", "1", 0);
	if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
		goto fail;
	}
	if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
		av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
		ret = AVERROR_OPTION_NOT_FOUND;
		goto fail;
	}

	mFormat_context->streams[stream_index]->discard = AVDISCARD_DEFAULT;
	switch (avctx->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		mAudio_decoder = new ff_decoder(avctx, mFormat_context->streams[stream_index], 26);
		mAudio_decoder->mCallback = mCallback;
		{
			mfreq = avctx->sample_rate;
			mchannels = avctx->channels;
			mchannel_layout = avctx->channel_layout;
			mfmt = avctx->sample_fmt;
			mAtimebase = avctx->time_base;
		}
		break;
	case AVMEDIA_TYPE_VIDEO:
		mVideo_decoder = new ff_decoder(avctx, mFormat_context->streams[stream_index], 26);
		mVideo_decoder->mCallback = mCallback;
			//init para
		{
			mWidth = avctx->width;
			mHeight=avctx->height;
			mpix_fmt=avctx->pix_fmt;
			mframe_rate = mFormat_context->streams[stream_index]->avg_frame_rate; //av_guess_frame_rate(mFormat_context, mFormat_context->streams[stream_index], NULL);
			mVtimebase.den = mframe_rate.num;
			mVtimebase.num = mframe_rate.den;
			mframe_aspect_ratio = avctx->sample_aspect_ratio;
		}
		break;
	default:
		break;
	}

fail:
	av_dict_free(&opts);

	return ret;
}

/* this thread gets the stream from the disk or the network */
int ff_demuxer::demux_thread(void *param)
{    
	ff_demuxer *demuxer = (ff_demuxer *)param;
	return demuxer->demux_loop();
}

int interrupt_cb(void *ctx)
{
	ff_demuxer *demuxer =(ff_demuxer*) ctx;
	return demuxer->mAbort;
}

void print_error(const char *filename, int err)
{
	char errbuf[128] = { 0 };
	const char *errbuf_ptr = errbuf;

	if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
		errbuf_ptr = strerror(AVUNERROR(err));
	av_log(NULL, AV_LOG_ERROR, "%s: %s\n", filename, errbuf_ptr);
}

static int is_realtime(AVFormatContext *s)
{
	if (!strcmp(s->iformat->name, "rtp")
		|| !strcmp(s->iformat->name, "rtsp")
		|| !strcmp(s->iformat->name, "sdp")
		)
		return 1;

	if (s->pb && (!strncmp(s->filename, "rtp:", 4)
		|| !strncmp(s->filename, "udp:", 4)
		)
		)
		return 1;
	return 0;
}

int ff_demuxer::put_nullpacket(int stream_index)
{
	AVPacket pkt1, *pkt = &pkt1;
	av_init_packet(pkt);
	pkt->data = NULL;
	pkt->size = 0;
	pkt->stream_index = stream_index;
	if (stream_index == mAudio_st_index)
		mAudio_decoder->push_back_packet(pkt);
	else
		mVideo_decoder->push_back_packet(pkt);

	return 0;
}

int ff_demuxer::open_input()
{
	int err, i, ret;
	AVDictionary *format_opts = NULL;
	int scan_all_pmts_set = 0;

	mVideo_st_index = mAudio_st_index = -1;

	mFormat_context = avformat_alloc_context();
	if (!mFormat_context) {
		av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
		ret = AVERROR(ENOMEM);
		goto fail;
	}
	mFormat_context->interrupt_callback.callback = interrupt_cb;
	mFormat_context->interrupt_callback.opaque = this;
	if (!av_dict_get(format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
		av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
		scan_all_pmts_set = 1;
	}
	av_dict_set(&format_opts, "framerate", "30", 0);
	if (!mFile_iformat)
		mFile_iformat = av_find_input_format(mInput_format);

	err = avformat_open_input(&mFormat_context, mInput, mFile_iformat, &format_opts);
	if (err < 0) {
		print_error(mInput, err);
		ret = -1;
		goto fail;
	}

	mFormat_context->flags |= AVFMT_FLAG_GENPTS;

	av_format_inject_global_side_data(mFormat_context);

	if (err < 0) {
		av_log(NULL, AV_LOG_WARNING,
			"%s: could not find codec parameters\n", mInput);
		ret = -1;
		goto fail;
	}

	if (mFormat_context->pb)
		mFormat_context->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

	mRealtime = is_realtime(mFormat_context);

	av_dump_format(mFormat_context, 0, mInput, 0);
	av_dict_free(&format_opts);
	return 0;
fail:
	av_dict_free(&format_opts);
	return -1;
}

int ff_demuxer::find_and_initialize_stream_decoders()
{
	int ret = 0;
	int st_index[AVMEDIA_TYPE_NB] = { 0 };
	memset(st_index, -1, sizeof(st_index));

	if (!mVideo_disable)
		st_index[AVMEDIA_TYPE_VIDEO] =
		av_find_best_stream(mFormat_context, AVMEDIA_TYPE_VIDEO,
		st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
	if (!mAudio_disable)
		st_index[AVMEDIA_TYPE_AUDIO] =
		av_find_best_stream(mFormat_context, AVMEDIA_TYPE_AUDIO,
		st_index[AVMEDIA_TYPE_AUDIO],
		st_index[AVMEDIA_TYPE_VIDEO],
		NULL, 0);

	/* open the streams */
	if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
		stream_component_open(st_index[AVMEDIA_TYPE_AUDIO]);
	}

	ret = -1;

	if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
		ret = stream_component_open(st_index[AVMEDIA_TYPE_VIDEO]);
	}

	return ret;
}
int ff_demuxer::demux_loop()
{
	int ret=0;

	AVPacket pktStack, *pkt = &pktStack;

	if (open_input() < 0)
		goto fail;

	if (find_and_initialize_stream_decoders() < 0)
		goto fail;
	
	mCallback->demuxer_ready_callback();

	mAudio_decoder->ff_decoder_start();
	mVideo_decoder->ff_decoder_start();

	for (;;) {
		if (mAbort)
			break;

		ret = av_read_frame(mFormat_context, pkt);
		if (ret < 0) {
			if ((ret == AVERROR_EOF || avio_feof(mFormat_context->pb)) && !mEof) {
				if (mAudio_st_index >= 0)
					put_nullpacket(mAudio_st_index);
				if (mVideo_st_index)
					put_nullpacket(mVideo_st_index);
				mEof = 1;
			}
			if (mFormat_context->pb && mFormat_context->pb->error)
				break;
			continue;
		}
		else {
			mEof = 0;
		}

//normalize the timebase of the pkt 
		AVRational stream_tb;
		if (pkt->stream_index == mAudio_st_index)
			stream_tb = mAudioStream_tb;
		else
			stream_tb = mVideoStream_tb;

		if (AV_NOPTS_VALUE != pkt->pts)
			pkt->pts = av_rescale_q(pkt->pts, stream_tb, std_tb_us);

		if (AV_NOPTS_VALUE != pkt->dts)
			pkt->dts = av_rescale_q(pkt->dts, stream_tb, std_tb_us);

		if (AV_NOPTS_VALUE != pkt->duration)
			pkt->duration = av_rescale_q(pkt->duration, stream_tb, std_tb_us);

		//debug
		if (AV_NOPTS_VALUE == pkt->dts || AV_NOPTS_VALUE == pkt->pts)
			TRACE("packet timestamps not valuable\n");

//set the start offset for correct pts
		if (AV_NOPTS_VALUE == mStart_pos){
			if (AV_NOPTS_VALUE != pkt->dts)
				mStart_pos = pkt->dts;
			else
				mStart_pos = pkt->pts;
		}
		if (AV_NOPTS_VALUE != pkt->pts)
		    pkt->pts -= mStart_pos;

		if (AV_NOPTS_VALUE != pkt->dts)
		    pkt->dts -= mStart_pos;

//if pts < 0 , discard the pkt;
		if (pkt->pts < 0)
			goto unref;

		if (pkt->stream_index == mAudio_st_index) {
			mAudio_decoder->push_back_packet(pkt);
		}else if (pkt->stream_index == mVideo_st_index) {
			mVideo_decoder->push_back_packet(pkt);
		}else {
unref:
			av_packet_unref(pkt);
		}
	}

	ret = 0;
fail:
	//if (mFormat_context)
		//avformat_close_input(&mFormat_context);
	return 0;
}

int ff_demuxer::demuxer_open(const char *filename, char *input_format, decoder_callback *callback)
{
	mInput = av_strdup(filename);
	if (!mInput)
		goto fail;

	mInput_format = av_strdup(input_format);
	mCallback = callback;

	mDemuxer_thread = new boost_thread(ff_demuxer::demux_thread, (void*)this);
	if (!mDemuxer_thread) {
		av_log(NULL, AV_LOG_FATAL, "demux_thread() err\n");
fail:
		demuxer_close();
		return -1;
	}
	return 0;
}