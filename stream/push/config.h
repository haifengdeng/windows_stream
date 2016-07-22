#ifndef FFMPEG_CONFIG_H
#define FFMPEG_CONFIG_H

#if defined(WIN32) && !defined(__cplusplus)
#define inline __inline
#endif
#define snprintf _snprintf

#define FFMPEG_CONFIGURATION ""
#define FFMPEG_LICENSE "LGPL version 2.1 or later"
#define CONFIG_THIS_YEAR 2016
#define CONFIG_AVDEVICE 1
#define CONFIG_AVFORMAT 1
#define CONFIG_AVUTIL 1
#define CONFIG_SWRESAMPLE 1
#define CONFIG_AVFILTER 1
#define CONFIG_AVRESAMPLE 0
#define CONFIG_AVCODEC 1

#include <queue>
#include "trace_debug.h"

#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <windows.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cassert>
#include <memory>

extern "C"{
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"

#if CONFIG_AVFILTER
# include "libavfilter/avfilter.h"
# include "libavfilter/buffersink.h"
# include "libavfilter/buffersrc.h"
#endif
extern AVInputFormat ff_dshowaa_demuxer;
}
#include "json/json.h"

class av_sync{
public:
	av_sync(){ 
		mSync_frame_ts = mSync_Sys_timestampe = 0; 
		mInitialized = false;
	}
	virtual ~av_sync(){}
	bool init_ok(){ return mInitialized == true; }
	void resetSync(int64_t sync_frame_ts);
	int64_t get_sys_ts(int64_t frame_ts);
	int64_t get_frame_ts(int64_t sys_ts);

	bool frame_out_of_bounds(int64_t frame_ts);
	bool frame_within_curent(int64_t frame_ts, uint64_t interval);
	bool frame_without_less(int64_t frame_ts, uint64_t interval);

private:
	//based on microsecond 
	int64_t mSync_frame_ts;
	int64_t mSync_Sys_timestampe;
	bool mInitialized;
	std::mutex  mMutex;
};

extern AVRational std_tb_us;
int64_t get_sys_time();
#endif // FFMPEG_CONFIG_H
