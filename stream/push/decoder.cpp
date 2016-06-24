#include "input.h"

ff_decoder::ff_decoder(AVCodecContext *codec_context,
	AVStream *stream ,unsigned int packet_queue_size)
{
	resetValue();

	assert(codec_context != NULL);
	assert(stream != NULL);


	mCodec = codec_context;
	mStream = stream;
	mAbort = false;
	mFinished = false;
	mPacket_pending = 0;

	mPacket_queue_size = packet_queue_size;

	mTimer_next_wake = (double)av_gettime() / 1000000.0;
	mPrevious_pts_diff = 40e-3;
	mCurrent_pts_time = av_gettime();
	mStart_pts = 0;
	mPredicted_pts = 0;
    mFirst_frame = true;
}
void ff_decoder::push_back_packet(AVPacket* pkt)
{
	mPacket_queue.push(*pkt);
}

int ff_decoder::decode_frame(AVFrame *frame, bool *frame_complete)
{
	int got_frame = 0;

	do {
		int ret = -1;
		if (mAbort)
			return -1;
		if (!mPacket_pending){
			AVPacket pkt;
			if (mPacket_queue.size() > 0) {

				pkt = mPacket_queue.front();
				mPacket_queue.pop();

				if (NULL == pkt.data || 0 == pkt.size){
					avcodec_flush_buffers(mCodec);
					mFinished = 1;
				}
			}
			av_packet_unref(&mPkt);
			mPkt_temp = mPkt = pkt;
			mPacket_pending = 1;

		}
	
		switch (mCodec->codec_type) {
		case AVMEDIA_TYPE_VIDEO:
			ret = avcodec_decode_video2(mCodec, frame, &got_frame, &mPkt_temp);
			if (got_frame) {
					frame->pts = frame->pkt_dts;
			}
			break;
		case AVMEDIA_TYPE_AUDIO:
			ret = avcodec_decode_audio4(mCodec, frame, &got_frame, &mPkt_temp);
			if (got_frame) {
				AVRational tb = { 1, frame->sample_rate };
				if (frame->pts != AV_NOPTS_VALUE)
					frame->pts = av_rescale_q(frame->pts, mCodec->time_base, tb);
				else if (frame->pkt_pts != AV_NOPTS_VALUE)
					frame->pts = av_rescale_q(frame->pkt_pts, av_codec_get_pkt_timebase(mCodec), tb);
			}
			break;
		}

		if (ret < 0) {
			mPacket_pending = 0;
		}
		else {
			mPkt_temp.dts =
				mPkt_temp.pts = AV_NOPTS_VALUE;
			if (mPkt_temp.data) {
				if (mCodec->codec_type != AVMEDIA_TYPE_AUDIO)
					ret = mPkt_temp.size;
				mPkt_temp.data += ret;
				mPkt_temp.size -= ret;
				if (mPkt_temp.size <= 0)
					mPacket_pending = 0;
			}
			else {
				if (!got_frame) {
					mPacket_pending = 0;
				}
			}
		}
	} while (!got_frame && !mFinished);

	return got_frame;
}

int ff_decoder::video_decoder_thread()
{
	return audio_decoder_thread();
}

int ff_decoder::audio_decoder_thread()
{
	bool frame_complete=0;
	AVFrame *frame = av_frame_alloc();
	int ret=0;

	while (!mAbort) {
		ret = decode_frame(frame, &frame_complete);
		if (ret == 0) {
			break;
		}

		// Did we get a audio frame?
		if (frame_complete) {
			// If we don't have a good PTS, try to guess based
			// on last received PTS provided plus prediction
			// This function returns a pts scaled to stream
			// time base
			//mFrame_queue.push(av_frame_clone(frame));
			if (mCodec->codec_type == AVMEDIA_TYPE_VIDEO)
				mCallback->video_callback(mStream, mCodec, av_frame_clone(frame));
			else
				mCallback->audio_callback(mStream, mCodec,av_frame_clone(frame));
			av_frame_unref(frame);
		}
	}

	av_frame_free(&frame);
	mFinished = true;

	return NULL;

}

int ff_decoder::decoder_thread(ff_decoder *decoder)
{
	if (decoder->mCodec->codec_type == AVMEDIA_TYPE_VIDEO)
		return decoder->video_decoder_thread();
	else
		return decoder->video_decoder_thread();
}


bool ff_decoder::ff_decoder_start()
{
	mDecoder_thread = new boost::thread(ff_decoder::decoder_thread, this);
	return true;
}


void ff_decoder::decoder_abort()
{
	mAbort = true;
	mDecoder_thread->join();
}