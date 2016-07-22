#include "output.h"

ffmpeg_output::ffmpeg_output(Json::Value &settings)
{
	mActive = false;
	mWrite_thread = NULL;
	mAbort = false;
	mff_data = NULL;

	mConfig = new ffmpeg_cfg();
	memset(mConfig, 0, sizeof(*mConfig));

	mConfig->url = av_strdup(settings["url"].asString().c_str());
	mConfig->format_name = av_strdup(settings["urlFmt"].asString().c_str());
	//audio
	mConfig->audioconfig.channels = settings["audioChannel"].asInt();
	mConfig->audioconfig.sample_rate = settings["audioSampleRate"].asInt();
	mConfig->audioconfig.sample_fmt = AV_SAMPLE_FMT_FLTP;
	mConfig->audioconfig.codecId = AV_CODEC_ID_AAC;
	mConfig->audioconfig.audio_bitrate = settings["AudioBitRate"].asInt();
	//video
	mConfig->videoconfig.pix_fmt = AV_PIX_FMT_YUV420P;
	mConfig->videoconfig.codecId = AV_CODEC_ID_H264;
	mConfig->videoconfig.width = settings["videoWidth"].asInt();
	mConfig->videoconfig.height = settings["videoHeight"].asInt();
	mConfig->videoconfig.frame_rate = settings["videoFps"].asInt();
	mConfig->videoconfig.video_bitrate = settings["videoBitRate"].asInt();
}

ffmpeg_output::~ffmpeg_output()
{
	while (mAudioPackets.size()){
		AVPacket packet = mAudioPackets.front();
		mAudioPackets.pop();
		av_packet_unref(&packet);
	}

	while (mVideoPackets.size()){
		AVPacket packet = mVideoPackets.front();
		mVideoPackets.pop();
		av_packet_unref(&packet);
	}

	if (mff_data)
		delete mff_data;

	if (mConfig)
		delete mConfig;
}
//virtual 
std::string ffmpeg_output::getOutputId()
{
	std::string id = "ffmepg_output";
	return id;
}
int ffmpeg_output::getOutputflags()
{
	return OUTPUT_AV;
}
bool ffmpeg_output::start()
{
	mff_data = new ffmpeg_data(*mConfig);
	mff_data->start();
	mWrite_thread = new boost_thread(write_thread_func, (void *)this);
	mActive = true;
	return true;
}
void ffmpeg_output::stop()
{
	mAbort = true;
	mActive = false;
	mWrite_thread->join();
	delete mWrite_thread;
	mWrite_thread = NULL;

	if (mff_data){
		mff_data->close();
		delete mff_data;
		mff_data = NULL;
	}

	while (mAudioPackets.size()){
		AVPacket packet = mAudioPackets.front();
		mAudioPackets.pop();
		av_packet_unref(&packet);
	}

	while (mVideoPackets.size()){
		AVPacket packet = mVideoPackets.front();
		mVideoPackets.pop();
		av_packet_unref(&packet);
	}
	return;
}
bool ffmpeg_output::isactived()
{
	return mActive;
}

void ffmpeg_output::raw_video(AVFrame *frames)
{
	AVPacket packet = { 0 };
	int ret = 0, got_packet = 0;

	av_init_packet(&packet);

	ret = mff_data->encode_video(&packet, frames, &got_packet);
	if (ret < 0){
		return;
	}
	if (got_packet){
		if (!push_back_packet(&packet, false))
			av_packet_unref(&packet);
	}
}
void ffmpeg_output::raw_audio(AVFrame *frames)
{
	AVPacket packet = { 0 };
	int ret = 0, got_packet;

	av_init_packet(&packet);

	ret = mff_data->encode_audio(&packet, frames, &got_packet);
	if (ret < 0){
		return;
	}
	if (got_packet){
		if (!push_back_packet(&packet, true))
			av_packet_unref(&packet);
	}
}
void ffmpeg_output::encoded_packet(AVPacket *pakcet)
{
	return;
}

void ffmpeg_output::update(Json::Value &settings)
{}

void ffmpeg_output::get_properties(Json::Value &props)
{}