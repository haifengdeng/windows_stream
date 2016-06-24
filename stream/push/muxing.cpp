#include "output.h"

int ffmpeg_data::writePacket(AVPacket*packet)
{
	int ret = 0;
	char errstr[AV_ERROR_MAX_STRING_SIZE];
	ret = av_interleaved_write_frame(mOutput, packet);
	if (ret < 0) {
		av_free_packet(packet);
		TRACE("receive_audio: Error writing packet: %d",
			av_make_error_string(errstr, AV_ERROR_MAX_STRING_SIZE, ret));
		return ret;
	}
	return 0;
}

ffmpeg_output::ffmpeg_output()
{
	resetValue();
}

void ffmpeg_output::push_back_packet(AVPacket *pkt)
{
	mWrite_mutex.lock();
	mPackets.push(*pkt);
	mWrite_mutex.unlock();
	mWrite_cv.notify_all();
}

int ffmpeg_output::process_packet()
{
	AVPacket packet;
	bool new_packet = false;
	int ret;

start:
	mWrite_mutex.lock();
	if (mPackets.size()) {
		packet = mPackets.front();
		mPackets.pop();
		new_packet = true;
	}
	else{
		new_packet = false;
	}
	mWrite_mutex.unlock();

	if (!new_packet)
		return 0;

	/*blog(LOG_DEBUG, "size = %d, flags = %lX, stream = %d, "
	"packets queued: %lu",
	packet.size, packet.flags,
	packet.stream_index, output->packets.num);*/

	ret = mff_data.writePacket(&packet);
	
	if (ret < 0 || mStop_event)
		goto End;
	goto start;
End:
	return 0;
}

int ffmpeg_output::write_thread()
{	
	while (mStop_event) {
		boost::unique_lock<boost::mutex> lck(mMutex_cv);
		boost::cv_status cv_ret = mWrite_cv.wait_for(lck,boost::chrono::milliseconds(10));
		if (cv_ret == boost::cv_status::timeout)
			continue;

		/* check to see if shutting down */
		if (mStop_event)
			break;

		int ret = process_packet();
		if (ret != 0) {
			break;
		}
	}
	mActive = false;
	return NULL;
}

int ffmpeg_output::write_thread_func(ffmpeg_output *output)
{
	return output->write_thread();
	
}


bool ffmpeg_output::start_output(struct ffmpeg_cfg*cfg)
{
	mff_data.ffmpeg_data_init(cfg);
	mWrite_thread = new boost::thread(write_thread_func, this);
	return true;
}

bool ffmpeg_output::close_output()
{
	mStop_event = true;
	mWrite_thread->join();
	mff_data.ffmpeg_data_close();
	return true;
}