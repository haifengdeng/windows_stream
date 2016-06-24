#include "input.h"
#include "output.h"

void av_sync::resetSync(int64_t sync_frame_ts)
{
	mMutex.lock();
	mSync_frame_ts = sync_frame_ts;
	mSync_Sys_timestampe = av_gettime_relative();
	mInitialized = true;
	mMutex.unlock();
}

int64_t av_sync::get_sys_ts(int64_t frame_ts)
{
	int64_t sys_ts = 0;
	sys_ts= (frame_ts - mSync_frame_ts) + mSync_Sys_timestampe;
	return sys_ts;
}

int64_t av_sync::get_frame_ts(int64_t sys_ts)
{
	return (sys_ts - mSync_Sys_timestampe) + mSync_frame_ts;
}

#define MAX_TS_VAR          2000000ULL

bool av_sync::frame_out_of_bounds(int64_t frame_ts)
{
	if (get_sys_ts(frame_ts) < get_sys_time())
		return ((get_sys_time() - get_sys_ts(frame_ts)) > MAX_TS_VAR);
	else
		return ((get_sys_ts(frame_ts) - get_sys_time()) > MAX_TS_VAR);
}

bool av_sync::frame_within_curent(int64_t frame_ts, uint64_t interval)
{
	if (get_sys_ts(frame_ts) < get_sys_time())
	{
		return (get_sys_time() - get_sys_ts(frame_ts)) < (interval / 2);
	}
	else
	{
		return (get_sys_ts(frame_ts) - get_sys_time()) < (interval / 2);
	}
}
bool av_sync::frame_without_less(int64_t frame_ts, uint64_t interval)
{
	if ((get_sys_ts(frame_ts) < get_sys_time()))
		 return (get_sys_time() - get_sys_ts(frame_ts)) > (interval / 2);
	return false;
}