#include "input.h"

ff_decoder::ff_decoder(AVCodecContext *codec_context,
	                   AVStream *stream,
					   source_callback * callback)
{
	assert(codec_context != NULL);
	assert(stream != NULL);
	assert(callback != NULL);


	mCodec = codec_context;
	mStream = stream;
	mCallback = callback;
	mAbort = false;
	mPacket_pending = false;

	mDecoder_thread = NULL;
	av_init_packet(&mPkt_temp);
	av_init_packet(&mPkt);
}

ff_decoder::~ff_decoder()
{
	decoder_abort();
}
void ff_decoder::pushback_packet(AVPacket* pkt)
{
	mPacketMutex.lock();

	if (mPacketQueue.size() < maxPacketQueueSize)
		mPacketQueue.push(*pkt);
	else
		av_packet_unref(pkt);

	mPacketMutex.unlock();
	mWrite_cv.notify_all();
	
}

int ff_decoder::decode_frame(AVFrame *frame, bool *frame_complete)
{
	int got_frame = 0;

	do {
		int ret = -1;
start:
		if (mAbort)
			return -1;

		if (!mPacket_pending){
			if (mPacketQueue.size() > 0) {
			    AVPacket pkt;
			    av_init_packet(&pkt);

				mPacketMutex.lock();
				pkt = mPacketQueue.front();
				mPacketQueue.pop();
				mPacketMutex.unlock();

				av_packet_unref(&mPkt);	
				if (NULL == pkt.data || 0 == pkt.size){
					avcodec_flush_buffers(mCodec);
				}
			    mPkt_temp = mPkt = pkt;
				mPacket_pending = 1;
			}
			else{
				std::unique_lock<std::mutex> lck(mMutex_cv);
				std::cv_status cv_ret = mWrite_cv.wait_for(lck, std::chrono::milliseconds(10));
				if (cv_ret == std::cv_status::timeout){
				    *frame_complete = false;
				    return 0;
				}
				goto start;
			}
		}
	
		switch (mCodec->codec_type) {
		case AVMEDIA_TYPE_VIDEO:
			ret = avcodec_decode_video2(mCodec, frame, &got_frame, &mPkt_temp);
			if (got_frame) {
			    frame->pts = frame->pkt_pts;
			}
			break;
		case AVMEDIA_TYPE_AUDIO:
			ret = avcodec_decode_audio4(mCodec, frame, &got_frame, &mPkt_temp);
			if (got_frame) {
				frame->pts = frame->pkt_pts;
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
	} while (!got_frame);

	*frame_complete = got_frame;

	return 0;
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
		if (ret < 0) {
			break;
		}

		if (frame_complete) {
			if (mCodec->codec_type == AVMEDIA_TYPE_VIDEO)
				mCallback->video_callback(av_frame_clone(frame));
			else
				mCallback->audio_callback(av_frame_clone(frame));
			av_frame_unref(frame);
		}
	}

	av_frame_free(&frame);
	av_packet_unref(&mPkt);
	av_init_packet(&mPkt);
	av_init_packet(&mPkt_temp);
	return 0;

}

int ff_decoder::decoder_thread(void *param)
{
	ff_decoder *decoder = (ff_decoder *)param;
	if (decoder->mCodec->codec_type == AVMEDIA_TYPE_VIDEO)
		return decoder->video_decoder_thread();
	else
		return decoder->audio_decoder_thread();
}


bool ff_decoder::ff_decoder_start()
{
	mDecoder_thread = new boost_thread(ff_decoder::decoder_thread, (void*)this);
	return true;
}


void ff_decoder::decoder_abort()
{
	mAbort = true;

	if (mDecoder_thread){
		mDecoder_thread->join();
		delete mDecoder_thread;
	}
	mDecoder_thread = NULL;
}