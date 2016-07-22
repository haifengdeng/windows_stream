#pragma once

#define MAX_AV_PLANES 8

/* time threshold in nanoseconds to ensure audio timing is as seamless as
* possible */
#define TS_SMOOTHING_THRESHOLD 70000ULL

enum speaker_layout {
	SPEAKERS_UNKNOWN,
	SPEAKERS_MONO,
	SPEAKERS_STEREO,
	SPEAKERS_2POINT1,
	SPEAKERS_QUAD,
	SPEAKERS_4POINT1,
	SPEAKERS_5POINT1,
	SPEAKERS_5POINT1_SURROUND,
	SPEAKERS_7POINT1,
	SPEAKERS_7POINT1_SURROUND,
	SPEAKERS_SURROUND,
};

static inline int get_audio_channels(enum speaker_layout speakers)
{
	switch (speakers) {
	case SPEAKERS_MONO:             return 1;
	case SPEAKERS_STEREO:           return 2;
	case SPEAKERS_2POINT1:          return 3;
	case SPEAKERS_SURROUND:
	case SPEAKERS_QUAD:             return 4;
	case SPEAKERS_4POINT1:          return 5;
	case SPEAKERS_5POINT1:
	case SPEAKERS_5POINT1_SURROUND: return 6;
	case SPEAKERS_7POINT1:          return 8;
	case SPEAKERS_7POINT1_SURROUND: return 8;
	case SPEAKERS_UNKNOWN:          return 0;
	}

	return 0;
}