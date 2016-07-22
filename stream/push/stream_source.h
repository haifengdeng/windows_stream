#pragma once

#include "config.h"
#include "audio_process/audio-resampler.h"
#include"audio_process\circlebuf.h"

struct wasapiSourceData{
	uint8_t *data[MAX_AV_PLANES];
	int frames;
	speaker_layout speakers;
	int samples_per_sec;
	AVSampleFormat format;
	int64_t timestamp;
};

struct wasapiAudioData{
	uint8_t             *data[MAX_AV_PLANES];
	uint32_t            frames;
	uint64_t            timestamp;
};

class stream_source
{
public:
	enum source_type {
		SOURCE_TYPE_INPUT,
		SOURCE_TYPE_FILTER,
		SOURCE_TYPE_TRANSITION,
		SOURCE_TYPE_SCENE,
	};

	enum propertytype{
		PROPERTYTYPE_AUDIO=1<<0,
		PROPERTYTYPE_VIDEO=1<<1,
	};

	enum source_flag{
		SOURCE_VIDEO,
		SOURCE_AUDIO,
		SOURCE_DATA,
	};
public:
	virtual void getSourceStringId(std::string & id) = 0;
	virtual int getSourceType() = 0;
	virtual int getSourceFlag() = 0;

	//
	virtual int activeSourceStream() = 0;
	virtual int deactiveSourceStream() = 0;
	virtual int updateSourceSetting(Json::Value & info) = 0;
	virtual bool isactive() = 0;

	//
	virtual void getSourceProperties(Json::Value &props, propertytype flag) = 0;
	//
	virtual bool getSourceFrame(AVFrame* frame, bool audio)
	{
		return false;
	}
	virtual AVFrame * getSourceFrame(bool audio){
		return NULL;
	}

	//
	virtual void  rawRender(AVFrame *frame){
	}
	//
protected:
	//for audio
	void source_output_audio_push_back(const wasapiAudioData *in);
	void source_output_audio_place(const wasapiAudioData *in);
	void reset_audio_data(uint64_t os_time);
	void reset_audio_timing(uint64_t timestamp, uint64_t os_time);
	void reset_resampler(wasapiSourceData & AudioData);
	void process_audio(wasapiSourceData &AudioData);
	void copy_audio_data(const uint8_t *const data[], uint32_t frames, uint64_t ts);
	void source_output_audio_data();
private:
	//for resample
	audio_resampler_t * mAudioResampler;
	struct resample_info  mResample_info;
	uint64_t              mResample_offset;
	bool mAudio_failed;


	volatile bool                   timing_set;
	volatile uint64_t               timing_adjust;
	uint64_t                        next_audio_ts_min;
	uint64_t                        next_audio_sys_ts_min;
	uint64_t                        last_frame_ts;
	uint64_t                        last_sys_timestamp;
	int64_t                         last_sync_offset;

	uint64_t                        audio_ts;
	struct circlebuf                audio_input_buf[2];


	struct wasapiAudioData mAudio_Data;
	size_t  mAudio_storage_size;
};

stream_source* CreateSource(Json::Value & info);
void getSourceDefaultSetting(Json::Value &setting);
