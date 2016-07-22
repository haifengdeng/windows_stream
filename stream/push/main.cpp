#include "push.h"
#include <signal.h>

bool  processAborted = false;

void SignalHandler(int signal)
{
	TRACE("recieve a signal:%d\n", signal);
	processAborted = true;
}

int main(int argc, char **argv)
{
	signal(SIGINT, SignalHandler);
	signal(SIGTERM, SignalHandler);
	init_ffmpeg();

	Json::Value inputFormat;
	Json::Value wasapiFormat;
	Json::Value outputFormat;

	//init input dshow capture format
	inputFormat["inputFmt"] = "dshow_pingan";
	stream_source::getSourceDefaultSetting(inputFormat);

	//int wasapi capture format
	wasapiFormat["inputFmt"] = "wasapi_pingan";
	stream_source::getSourceDefaultSetting(wasapiFormat);

	/*init output format*/
	//outputFormat["url"] = "rtmp://10.128.164.55:1990/live/stream_test5";
	outputFormat["url"] = "D:/pushtest.flv";
	outputFormat["urlFmt"] = "flv";
	//video
	outputFormat["videoWidth"] = inputFormat["videoWidth"];
	outputFormat["videoHeight"] = inputFormat["videoHeight"];
	outputFormat["videoFps"] = inputFormat["videoFps"];
	outputFormat["videoBitRate"] = 1000;
	//audio
	outputFormat["audioSampleRate"] = inputFormat["audioSampleRate"];
	outputFormat["audioChannel"] = inputFormat["audioChannel"];
	outputFormat["AudioBitRate"] = 128;


	push *_push = new push(inputFormat, wasapiFormat, outputFormat);
	_push->start_push();


	while (!processAborted)
	{
		av_usleep(1000);
		int ch = getchar();
		if (ch == 'q')
			break;

	}
	_push->stop_push();
	delete _push;
	return 0;
}