#pragma once

#include "libavformat/avformat.h"
#include "media-io-defs.h"

AVSampleFormat audio_output_get_format();
int audio_output_get_sample_rate();
enum speaker_layout audio_output_get_speakers();
int32_t audio_output_get_planes();
int32_t audio_output_get_block_size();
int audio_output_get_channels();