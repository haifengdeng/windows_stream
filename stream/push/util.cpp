#include "config.h"
static const char *astrblank = "";
int astrcmpi_n(const char *str1, const char *str2, size_t n)
{
	if (!n)
		return 0;
	if (!str1)
		str1 = astrblank;
	if (!str2)
		str2 = astrblank;

	do {
		char ch1 = (char)toupper(*str1);
		char ch2 = (char)toupper(*str2);

		if (ch1 < ch2)
			return -1;
		else if (ch1 > ch2)
			return 1;
	} while (*str1++ && *str2++ && --n);

	return 0;
}

static enum AVCodecID get_codec_id(const char *name, int id)
{
	AVCodec *codec;

	if (id != 0)
		return (enum AVCodecID)id;

	if (!name || !*name)
		return AV_CODEC_ID_NONE;

	codec = avcodec_find_encoder_by_name(name);
	if (!codec)
		return AV_CODEC_ID_NONE;

	return codec->id;
}

int64_t get_sys_time()
{
	return av_gettime_relative();
}

