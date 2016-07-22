#include "stdafx.h"
#include "stream_source.h"
#include "input.h"
#include "wasapiSource.h"

stream_source* CreateSource(Json::Value & info)
{
	stream_source *source = NULL;
	std::string inputFmt = info["inputFmt"].asString();
	if (0 == inputFmt.compare("dshow_pingan"))
		source = new ffmpeg_source(info);
	else if (0 == inputFmt.compare("wasapi_pingan")){
		source = new wasapiSource(info, false);
	}
	return source;
}

void getSourceDefaultSetting(Json::Value &setting)
{
	std::string inputFmt = setting["inputFmt"].asString();

	if (0 == inputFmt.compare("dshow_pingan")){
		ffmpeg_source::getSourceDefaultSetting(setting);
	}
	else if (0 == inputFmt.compare("wasapi_pingan")){
		wasapiSource::getSourceDefaultSetting(setting);
	}
}



void stream_source::reset_resampler(wasapiSourceData & AudioData)
{
	struct resample_info output_info;

	output_info.format = audio_output_get_format();
	output_info.samples_per_sec = audio_output_get_sample_rate();
	output_info.speakers = audio_output_get_speakers();

	mResample_info.format = AudioData.format;
	mResample_info.samples_per_sec = AudioData.samples_per_sec;
	mResample_info.speakers = AudioData.speakers;

	audio_resampler_destroy(mAudioResampler);
	mAudioResampler = NULL;
	mResample_offset = 0;

	if (output_info.samples_per_sec == mResample_info.samples_per_sec &&
		output_info.format == mResample_info.format          &&
		output_info.speakers == mResample_info.speakers) {
		mAudio_failed = false;
		return;
	}

	mAudioResampler = audio_resampler_create(&output_info,
		&mResample_info);

	if (mAudioResampler == NULL){
		TRACE("creation of resampler failed");
		mAudio_failed = true;
	}
}

void stream_source::copy_audio_data(const uint8_t *const data[], uint32_t frames, uint64_t ts)
{
	size_t planes = audio_output_get_planes();
	size_t blocksize = audio_output_get_block_size();
	size_t size = (size_t)frames * blocksize;
	bool   resize = mAudio_storage_size < size;

	mAudio_Data.frames = frames;
	mAudio_Data.timestamp = ts;

	for (size_t i = 0; i < planes; i++) {
		/* ensure audio storage capacity */
		if (resize) {
			av_free(mAudio_Data.data[i]);
			mAudio_Data.data[i] = (uint8_t*)av_malloc(size);
		}

		memcpy(mAudio_Data.data[i], data[i], size);
	}

	if (resize)
		mAudio_storage_size = size;
}


void stream_source::process_audio(wasapiSourceData &AudioData)
{
	uint32_t frames = AudioData.frames;
	if (mResample_info.samples_per_sec != AudioData.samples_per_sec ||
		mResample_info.format != AudioData.format ||
		mResample_info.speakers != AudioData.speakers)
		reset_resampler(AudioData);

	if (mAudio_failed)
		return;

	if (mAudioResampler){
		uint8_t *output[MAX_AV_PLANES];
		memset(output, 0, sizeof(output));
		audio_resampler_resample(mAudioResampler,
			output, &frames, &mResample_offset, AudioData.data, AudioData.frames);
		copy_audio_data(output, frames, AudioData.timestamp);
	}
	else{
		copy_audio_data(AudioData.data, AudioData.frames, AudioData.timestamp);
	}

}

static inline uint64_t uint64_diff(uint64_t ts1, uint64_t ts2)
{
	return (ts1 < ts2) ? (ts2 - ts1) : (ts1 - ts2);
}

#define MAX_TS_VAR          2000000ULL

static inline uint64_t conv_frames_to_time(const size_t sample_rate,
	const size_t frames)
{
	return (uint64_t)frames * 1000000000ULL / (uint64_t)sample_rate;
}

#define MAX_BUF_SIZE        (1000 * 1024 * sizeof(float))


void stream_source::source_output_audio_push_back(const wasapiAudioData *in)
{
	size_t channels = audio_output_get_channels();
	size_t size = in->frames * sizeof(float);

	/* do not allow the circular buffers to become too big */
	if ((audio_input_buf[0].size + size) > MAX_BUF_SIZE)
		return;

	for (size_t i = 0; i < channels; i++)
		circlebuf_push_back(&audio_input_buf[i],
		in->data[i], size);

}


static inline size_t get_buf_placement(int samplerate, uint64_t offset)
{
	return (size_t)(offset * (uint64_t)samplerate / 1000000000ULL);
}

void stream_source::reset_audio_data(uint64_t os_time)
{
	for (size_t i = 0; i < 2; i++) {
		if (audio_input_buf[i].size)
			circlebuf_pop_front(&audio_input_buf[i], NULL,
			audio_input_buf[i].size);
	}
	last_audio_input_buf_size = 0;
	audio_ts = os_time;
}

void stream_source::source_output_audio_place(const wasapiAudioData *in)
{
	size_t buf_placement;
	size_t channels = audio_output_get_channels();
	size_t size = in->frames * sizeof(float);

	if (!audio_ts || in->timestamp < audio_ts)
		reset_audio_data(in->timestamp);

	buf_placement = get_buf_placement(audio_output_get_sample_rate(),
		in->timestamp - audio_ts) * sizeof(float);

#if DEBUG_AUDIO == 1
	blog(LOG_DEBUG, "frames: %lu, size: %lu, placement: %lu, base_ts: %llu, ts: %llu",
		(unsigned long)in->frames,
		(unsigned long)source->audio_input_buf[0].size,
		(unsigned long)buf_placement,
		source->audio_ts,
		in->timestamp);
#endif

	/* do not allow the circular buffers to become too big */
	if ((buf_placement + size) > MAX_BUF_SIZE)
		return;

	for (size_t i = 0; i < channels; i++) {
		circlebuf_place(&audio_input_buf[i], buf_placement,
			in->data[i], size);
		circlebuf_pop_back(&audio_input_buf[i], NULL,
			audio_input_buf[i].size -
			(buf_placement + size));
	}

}
void stream_source::source_output_audio_data()
{
	int sample_rate = audio_output_get_sample_rate();
	struct wasapiAudioData in = mAudio_Data;
	uint64_t diff;
	uint64_t os_time = get_sys_time();
	int64_t sync_offset;
	bool using_direct_ts = false;
	bool push_back = false;

	if (uint64_diff(mAudio_Data.timestamp, os_time) < MAX_TS_VAR) {
		timing_adjust = 0;
		timing_set = true;
		using_direct_ts = true;
	}
	if (!timing_set) {
		reset_audio_timing(in.timestamp, os_time);

	}
	else if (next_audio_ts_min != 0) {
		diff = uint64_diff(next_audio_ts_min, in.timestamp);

		/* smooth audio if within threshold */
		if (diff > MAX_TS_VAR && !using_direct_ts)
			reset_audio_timing(in.timestamp, os_time);
		else if (diff < TS_SMOOTHING_THRESHOLD)
			in.timestamp = next_audio_ts_min;
	}

	next_audio_ts_min = in.timestamp +
		conv_frames_to_time(sample_rate, in.frames);

	in.timestamp += timing_adjust;

	mMutex.lock();
	if (next_audio_sys_ts_min == in.timestamp) {
		push_back = true;

	}
	else if (next_audio_sys_ts_min) {
		diff = uint64_diff(next_audio_sys_ts_min, in.timestamp);

		if (diff < TS_SMOOTHING_THRESHOLD) {
			push_back = true;

			/* This typically only happens if used with async video when
			* audio/video start transitioning in to a timestamp jump.
			* Audio will typically have a timestamp jump, and then video
			* will have a timestamp jump.  If that case is encountered,
			* just clear the audio data in that small window and force a
			* resync.  This handles all cases rather than just looping. */
		}
		else if (diff > MAX_TS_VAR) {
			reset_audio_timing(mAudio_Data.timestamp, os_time);
			in.timestamp = mAudio_Data.timestamp + timing_adjust;
		}
	}

	sync_offset = sync_offset;
	in.timestamp += sync_offset;
	in.timestamp -= mResample_offset;

	next_audio_sys_ts_min = next_audio_ts_min + timing_adjust;

	if (last_sync_offset != sync_offset) {
		if (last_sync_offset)
			push_back = false;
		last_sync_offset = sync_offset;
	}

	if (push_back && audio_ts)
		source_output_audio_push_back(&in);
	else
		source_output_audio_place(&in);
	mMutex.unlock();
}
void stream_source::reset_audio_timing(uint64_t timestamp, uint64_t os_time)
{
	timing_set = true;
	timing_adjust = os_time - timestamp;
}