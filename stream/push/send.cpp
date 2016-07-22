#include"output.h"

int ffmpeg_data::writePacket(AVPacket*packet)
{
	int ret = 0;
	char errstr[AV_ERROR_MAX_STRING_SIZE]="";

	if (AV_NOPTS_VALUE == mStartPTS){
		if (AV_NOPTS_VALUE != packet->dts)
			mStartPTS = packet->dts;
		else
			mStartPTS = packet->pts;
	}
	if (AV_NOPTS_VALUE != packet->pts)
		packet->pts -= mStartPTS;

	if (AV_NOPTS_VALUE != packet->dts)
		packet->dts -= mStartPTS;

	TRACE("%s pts=%d stream=%d size=%d\n", __FUNCTION__, 
		(int)packet->pts, packet->stream_index, packet->size);

	//ret = av_interleaved_write_frame(mOutput, packet);
	ret = av_write_frame(mOutput, packet);
	if (ret < 0) {
		av_packet_unref(packet);
		TRACE("receive_audio: Error writing packet: %s",
			av_make_error_string(errstr, AV_ERROR_MAX_STRING_SIZE, ret));
		return ret;
	}
	av_packet_unref(packet);
	return 0;
}
