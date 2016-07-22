#include "input.h"

ff_demuxer::ff_demuxer(input_config *config, source_callback*callback)
{ 
	mConfig = config;
	mCallback = callback;

	mIoContext = NULL;
	mFmtContext = NULL;
	mInputFormat = NULL;

	mAudio_decoder = NULL;
	mVideo_decoder = NULL;

	mDemuxer_thread = NULL;

	mAudioStream_tb = std_tb_us;
	mVideoStream_tb = std_tb_us;
	mAudioIndex = -1;
	mVideoIndex = -1;

	mStartPTS = AV_NOPTS_VALUE;
	mAbort = false;
}
ff_demuxer::~ff_demuxer()
{
	stopDemuxer();
}

bool ff_demuxer::isAborted()
{
	return mAbort;
}
int interrupt_cb(void *ctx)
{
	ff_demuxer *demuxer = (ff_demuxer*)ctx;
	return demuxer->isAborted();
}

void print_error(const char *filename, int err)
{
	char errbuf[128] = { 0 };
	const char *errbuf_ptr = errbuf;

	if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
		errbuf_ptr = strerror(AVUNERROR(err));
	av_log(NULL, AV_LOG_ERROR, "%s: %s\n", filename, errbuf_ptr);
}

void ff_demuxer::pushEmptyPacket()
{
	AVPacket pkt;
	av_init_packet(&pkt);

	pkt.data = NULL;
	pkt.size = 0;

	if (mAudio_decoder){
		pkt.stream_index = mAudioIndex;
		mAudio_decoder->pushback_packet(&pkt);
	}

	if (mVideo_decoder){
		pkt.stream_index = mVideoIndex;
		mVideo_decoder->pushback_packet(&pkt);
	}
}

void ff_demuxer::stream_component_close(int streamIndex)
{
	AVCodecContext *avctx = mFmtContext->streams[streamIndex]->codec;

	switch (avctx->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		mAudio_decoder->decoder_abort();
		delete mAudio_decoder;
		mAudio_decoder = NULL;
		break;
	case AVMEDIA_TYPE_VIDEO:
		mVideo_decoder->decoder_abort();
		delete mVideo_decoder;
		mVideo_decoder = NULL;
		break;
	default:
		break;
	}
	avcodec_close(avctx);
}

void ff_demuxer::stopDemuxer()
{
	mAbort = 1;
	if (mDemuxer_thread){
		mDemuxer_thread->join();
		delete mDemuxer_thread;
		mDemuxer_thread = NULL;
	}
	/* close each stream */
	if (mAudioIndex >= 0)
		stream_component_close(mAudioIndex);
	mAudioIndex = -1;

	if (mVideoIndex >= 0)
		stream_component_close(mVideoIndex);
	mVideoIndex = -1;

	avformat_close_input(&mFmtContext);
	av_freep(&mFmtContext);
}

int ff_demuxer::stream_component_open(int streamIndex)
{
	AVCodec *codec=NULL;
	AVDictionary *opts=NULL;
	AVDictionaryEntry *t = NULL;
	int ret = 0;

	AVCodecContext* avctx = mFmtContext->streams[streamIndex]->codec;
    codec = avcodec_find_decoder(avctx->codec_id);
	if (!codec) {
		av_log(NULL, AV_LOG_WARNING,
			"No codec could be found with id %d\n", avctx->codec_id);
		return -1;
	}

	av_codec_set_pkt_timebase(avctx, std_tb_us);

	avctx->codec_id = codec->id;
	avctx->flags2 |= AV_CODEC_FLAG2_FAST;

	if (!av_dict_get(opts, "threads", NULL, 0))
		 av_dict_set(&opts, "threads", "auto", 0);
	if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || 
		avctx->codec_type == AVMEDIA_TYPE_AUDIO)
		av_dict_set(&opts, "refcounted_frames", "1", 0);
	if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
		goto fail;
	}

	switch (avctx->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		mAudio_decoder = new ff_decoder(avctx, mFmtContext->streams[streamIndex], mCallback);
		break;
	case AVMEDIA_TYPE_VIDEO:
		mVideo_decoder = new ff_decoder(avctx, mFmtContext->streams[streamIndex],mCallback);
		break;
	default:
		break;
	}

fail:
	av_dict_free(&opts);
	return ret;
}

int ff_demuxer::find_and_initialize_stream_decoders()
{
	int ret = 0;
	mAudioIndex = -1;
	mVideoIndex = -1;
	for (int i = 0; i < mFmtContext->nb_streams; i++)
	{
		AVStream *st = mFmtContext->streams[i];
		AVCodecContext *avctx = st->codec;

		if (AVMEDIA_TYPE_AUDIO == avctx->codec_type){
			mAudioIndex = i;
			mAudioStream_tb = mFmtContext->streams[i]->time_base;
		}
		else if (AVMEDIA_TYPE_VIDEO == avctx->codec_type){
			mVideoIndex = i;
			mVideoStream_tb = mFmtContext->streams[i]->time_base;
		}

	}

	/* open the streams */
	if (mVideoIndex >= 0) {
		ret = stream_component_open(mVideoIndex);
	}

	if (mAudioIndex >= 0) {
		ret = stream_component_open(mAudioIndex);
	}

	return ret;
}

void ff_demuxer::setFmtOpts(AVDictionary ** fmt_opts)
{
	if (NULL == fmt_opts)
		return;

	//video
	if (mConfig->pix_fmt != AV_PIX_FMT_NONE){
		const char * pix_format = av_get_pix_fmt_name(mConfig->pix_fmt);
		av_dict_set(fmt_opts, "pixel_format", pix_format, 0);
	}
	if (mConfig->width &&mConfig->height){
		char video_size[16];
		snprintf(video_size, sizeof(video_size), "%dx%d", mConfig->width, mConfig->height);
		av_dict_set(fmt_opts, "video_size", video_size, 0);
	}

	if (mConfig->Fps){
		char framerate[16];
		snprintf(framerate, sizeof(framerate), "%d", mConfig->Fps);
		av_dict_set(fmt_opts, "framerate", framerate, 0);
	}
	//audio
	if (mConfig->channel){
		char channels[16];
		snprintf(channels, sizeof(channels), "%d", mConfig->channel);
		av_dict_set(fmt_opts, "channels", channels, 0);
	}
	
	if (mConfig->bitsPerSample){
		char sample_size[16];
		snprintf(sample_size, sizeof(sample_size), "%d", mConfig->bitsPerSample);
		av_dict_set(fmt_opts, "sample_size", sample_size, 0);
	}

	if (mConfig->frequency){
		char sample_rate[16];
		snprintf(sample_rate, sizeof(sample_rate), "%d", mConfig->frequency);
		av_dict_set(fmt_opts, "sample_rate", sample_rate, 0);
	}	
	
	//av_dict_set(fmt_opts, "audio_buffer_size", "50", 0);
}

int ff_demuxer::open_input()
{
	int ret = 0;
	AVDictionary *format_opts = NULL;

	setFmtOpts(&format_opts);
	mFmtContext = avformat_alloc_context();
	if (!mFmtContext) {
		av_log(NULL, AV_LOG_FATAL, "Could not allocate context.\n");
		ret = AVERROR(ENOMEM);
		goto fail;
	}

	mFmtContext->interrupt_callback.callback = interrupt_cb;
	mFmtContext->interrupt_callback.opaque = this;


	if (!mInputFormat)
		mInputFormat = av_find_input_format(mConfig->input_format);

	ret = avformat_open_input(&mFmtContext, mConfig->input, mInputFormat, &format_opts);
	if (ret < 0) {
		print_error(mConfig->input, ret);
		goto fail;
	}

	mFmtContext->flags |= AVFMT_FLAG_GENPTS;
	av_format_inject_global_side_data(mFmtContext);

	if (mFmtContext->pb)
		mFmtContext->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

	av_dump_format(mFmtContext, 0, mConfig->input, 0);
	av_dict_free(&format_opts);
	return 0;
fail:
	av_dict_free(&format_opts);
	if (mFmtContext){
		avformat_close_input(&mFmtContext);
	}

	return -1;
}

int ff_demuxer::demuxer_loop()
{
	int ret=0;

	AVPacket pktStack, *pkt = &pktStack;

	if (open_input() < 0)
		goto End;

	if (find_and_initialize_stream_decoders() < 0)
		goto End;

	mAudio_decoder->ff_decoder_start();
	mVideo_decoder->ff_decoder_start();

	for (;;) {
		if (mAbort)
			break;

		ret = av_read_frame(mFmtContext, pkt);
		if (ret < 0) {
			if ((ret == AVERROR_EOF || avio_feof(mIoContext))) {
				pushEmptyPacket();
				goto End;
			}
		}

        //normalize timebase 
		AVRational stream_tb;
		if (pkt->stream_index == mAudioIndex)
			stream_tb = mAudioStream_tb;
		else
			stream_tb = mVideoStream_tb;

		if (AV_NOPTS_VALUE != pkt->pts)
			pkt->pts = av_rescale_q(pkt->pts, stream_tb, std_tb_us);

		if (AV_NOPTS_VALUE != pkt->dts)
			pkt->dts = av_rescale_q(pkt->dts, stream_tb, std_tb_us);

		if (AV_NOPTS_VALUE != pkt->duration)
			pkt->duration = av_rescale_q(pkt->duration, stream_tb, std_tb_us);

		TRACE("ff_demuxer::demuxer_loop:%lld\n", pkt->pts);

		//debug
		if (AV_NOPTS_VALUE == pkt->dts || AV_NOPTS_VALUE == pkt->pts)
			TRACE("packet timestamps not valuable\n");

#if 0
        //set the start offset for correct pts
		if (AV_NOPTS_VALUE == mStartPTS){
			if (AV_NOPTS_VALUE != pkt->dts)
				mStartPTS = pkt->dts;
			else
				mStartPTS = pkt->pts;
		}
		if (AV_NOPTS_VALUE != pkt->pts)
			pkt->pts -= mStartPTS;

		if (AV_NOPTS_VALUE != pkt->dts)
			pkt->dts -= mStartPTS;
#endif
        //if pts < 0 , discard the pkt;
		if (pkt->pts < 0)
			goto unref;

		if (pkt->stream_index == mAudioIndex) {
			mAudio_decoder->pushback_packet(pkt);
		}else if (pkt->stream_index == mVideoIndex) {
			mVideo_decoder->pushback_packet(pkt);
		}else {
unref:
			av_packet_unref(pkt);
		}
	}
End:
	return 0;
}

int ff_demuxer::demuxer_thread(void *param)
{
	ff_demuxer *demuxer = (ff_demuxer *)param;
	return demuxer->demuxer_loop();
}

int ff_demuxer::startDemuxer()
{
	if (NULL == mConfig || NULL == mCallback)
		return -1;

	mDemuxer_thread = new boost_thread(ff_demuxer::demuxer_thread, (void*)this);
	if (!mDemuxer_thread) {
		av_log(NULL, AV_LOG_FATAL, "crate demuxer thread failed!\n");
		return -1;
	}
	return 0;
}