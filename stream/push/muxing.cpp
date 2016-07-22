#include "output.h"


bool ffmpeg_output::push_back_packet(AVPacket *pkt,bool audio)
{
	bool pushed = false;
	mWrite_mutex.lock();
	if (audio){
		if (mAudioPackets.size() < maxPacketsArraySize){
			mAudioPackets.push(*pkt);
			pushed = true;
		}
	}
	else{
		if (mVideoPackets.size() < maxPacketsArraySize){
			mVideoPackets.push(*pkt);
			pushed = true;
		}
	}
	mWrite_mutex.unlock();
	mWrite_cv.notify_all();

	if (false == pushed)
	{
		TRACE("%s:drop %s packet\n",__FUNCTION__ , audio ? "audio" : "video");
	}
	return pushed;
}

int ffmpeg_output::process_packet()
{
	AVPacket packet;
	
	bool new_packet = false;
	int ret;

start:
	new_packet = false;
	av_init_packet(&packet);

	mWrite_mutex.lock();
	if ((mAudioPackets.size() > 0) && (mVideoPackets.size() > 0)){
		if (mAudioPackets.front().dts <= mVideoPackets.front().dts){
			packet = mAudioPackets.front();
			mAudioPackets.pop();
		}else{
			packet = mVideoPackets.front();
			mVideoPackets.pop();
		}
		new_packet = true;
	}
	else{
		new_packet = false;
	}
	mWrite_mutex.unlock();

	if (!new_packet)
		return 0;

	if (packet.pts == AV_NOPTS_VALUE)
	{
		TRACE("%s:packet invalid\n",__FUNCTION__);
	}


	ret = mff_data->writePacket(&packet);
	
	if (ret < 0 || mAbort)
		goto End;

	goto start;
End:
	return 0;
}

int ffmpeg_output::write_thread()
{	
	while (!mAbort) {
		std::unique_lock<std::mutex> lck(mMutex_cv);
		std::cv_status cv_ret = mWrite_cv.wait_for(lck,std::chrono::milliseconds(10));
		if (cv_ret == std::cv_status::timeout)
			continue;

		/* check to see if shutting down */
		if (mAbort)
			break;

		int ret = process_packet();
		if (ret != 0) {
			break;
		}
	}
	mActive = false;
	return NULL;
}

int ffmpeg_output::write_thread_func(void *param)
{
	ffmpeg_output *output = (ffmpeg_output*)param;
	return output->write_thread();
	
}
