#pragma once
#include "config.h"
class stream_output
{
public:
	enum output_flag{
		OUTPUT_VIDEO = (1 << 0),
		OUTPUT_AUDIO = (1 << 1),
		OUTPUT_AV = (OUTPUT_VIDEO | OUTPUT_AUDIO),
		OUTPUT_ENCODED = (1 << 2),
		OUTPUT_SERVICE = (1 << 3),

	};

		virtual std::string getOutputId() = 0;
		virtual int getOutputflags() = 0;

		virtual bool start() = 0;
		virtual void stop() = 0;
		virtual bool isactived() = 0;

		virtual void raw_video(AVFrame *frames) = 0;
		virtual void raw_audio(AVFrame *frames) = 0;

		virtual void encoded_packet(AVPacket *pakcet) = 0;

		virtual void update(Json::Value &settings) = 0;

		virtual void get_properties(Json::Value &props) = 0;
};

