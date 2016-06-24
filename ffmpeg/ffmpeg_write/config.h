#ifndef FFMPEG_CONFIG_H
#define FFMPEG_CONFIG_H
#if defined(WIN32) && !defined(__cplusplus)
#define inline __inline
#endif
#define AVCONV_DATADIR   "./ffmpeg_write"
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
#define snprintf _snprintf

/* median of 3 */
#ifndef mid_pred
#define mid_pred mid_pred
static inline  int mid_pred(int a, int b, int c)
{
#if 0
	int t = (a - b)&((a - b) >> 31);
	a -= t;
	b += t;
	b -= (b - c)&((b - c) >> 31);
	b += (a - b)&((a - b) >> 31);

	return b;
#else
	if (a>b){
		if (c>b){
			if (c>a) b = a;
			else    b = c;
		}
	}
	else{
		if (b>c){
			if (c>a) b = c;
			else    b = a;
		}
	}
	return b;
#endif
}
#endif


#endif // FFMPEG_CONFIG_H
