#pragma once

#include "../config.h"

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define NO_DSHOW_STRSAFE
#include <dshow.h>
#include <dvdmedia.h>
#include "objidl.h"
#include "shlwapi.h"

struct VideoStreamCAPS;
struct AudioStreamCAPS;
class Dshow_monitor
{
public:
	Dshow_monitor();
	~Dshow_monitor();

	void chooseAudioDeviceAndConfig(Json::Value & audioSettings);
	void chooseVideoDeviceAndConfig(Json::Value & videoSettings);

private:
	typedef std::vector<struct VideoStreamCAPS> VideoDeviceCAPS;
	typedef struct AudioStreamCAPS    AudioDeviceCAPS;
	typedef char* unique_name_ptr;
	typedef char* frendly_name_ptr;

	int flushDeviceList(bool audio);
	void clear();
	int getDeviceCaps(unique_name_ptr id, IBaseFilter *device_filter, bool audio);
	void dshow_cycle_formats(IPin *pin, unique_name_ptr id, bool audio);

	//based on device unique_name;
	std::map<unique_name_ptr, VideoDeviceCAPS> mVideoCaps;
	std::map<unique_name_ptr, AudioDeviceCAPS> mAudioCaps;

	std::map<unique_name_ptr, frendly_name_ptr> mVideoDevice;
	std::map<unique_name_ptr, frendly_name_ptr> mAudioDevice;

};

