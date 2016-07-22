#include "input.h"
#include "output.h"
#include "filter.h"
#include "videoDisplay.h"
#include "dshow\Dshow_monitor.h"

class push{
public:
	push(Json::Value & inputsettings, Json::Value & wasapisettings, Json::Value & outputsettings);
	virtual ~push();
	int start_push();
	int stop_push();
	static int video_thread(void *param);
	static int audio_thread(void *param);
	int video_push_thread();
	int audio_push_thread();

private:
	int64_t getMediaInterval(bool audio);
	void video_sleep(uint64_t *p_time, uint64_t interval_us);
	void audio_sleep(uint64_t *p_time, uint64_t interval_us);
	bool getClosedAudioFrame(uint64_t audio_time, AVFrame *frame, uint64_t interval);
	bool getClosedVideoFrame(uint64_t video_time, AVFrame*frame, uint64_t interval);

private:
	//settings
	Json::Value mInput_JsonSetting;
	Json::Value mWasapi_JsonSetting;
	Json::Value mOutput_JsonSetting;

	av_sync        mSync;
	ffmpeg_output *mOutput;
	stream_source * mStreamSource;
	stream_source * mWasStreamSource;


	boost_thread *mVideoThread;
	boost_thread *mAudioThread;
	videoDisplay *mVideoDisplay;
	bool mAbort;
	bool mInited;

	//for record
	uint64_t mCountVideoFrame;
	uint64_t mCountlaggedVideoFrames;
	uint64_t mCountAudioFrame;
	uint64_t mCountlaggedAudioFrame;

	bool mAudioPending;
	bool mVideoPending;
	AVFrame *mVideoPendingframe;
	AVFrame *mAudioPendingframe;

};

int init_ffmpeg();