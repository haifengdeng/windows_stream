/**
* 最简单的基于FFmpeg的推流器（推送RTMP）
* Simplest FFmpeg Streamer (Send RTMP)
*
* 雷霄骅 Lei Xiaohua
* leixiaohua1020@126.com
* 中国传媒大学/数字电视技术
* Communication University of China / Digital TV Technology
* http://blog.csdn.net/leixiaohua1020
*
* 本例子实现了推送本地视频至流媒体服务器（以RTMP为例）。
* 是使用FFmpeg进行流媒体推送最简单的教程。
*
* This example stream local media files to streaming media
* server (Use RTMP as example).
* It's the simplest FFmpeg streamer.
*
*/


#include <stdio.h>
#define __STDC_CONSTANT_MACROS
#define __STDC_FORMAT_MACROS

#ifdef _WIN32
//Windows
extern "C"
{
#include "libavformat/avformat.h"
#include "libavutil/mathematics.h"
#include "libavutil/time.h"
#include "libavdevice/avdevice.h"
#include <windows.h>
#include <signal.h>
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavcodec/avcodec.h"
//#include "libavutil/timestamp.h"
};
#else
//Linux...
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavutil/time.h>
#include <libavdevice/avdevice.h>
#ifdef __cplusplus
};
#endif
#endif

const char* in_filename = "video=Integrated Camera:audio=External Mic (Realtek High Defi";
const char* out_filename = "D:/capture.flv";
static volatile int received_sigterm = 0;
static volatile int received_nb_signals = 0;
static volatile int transcode_init_done = 0;
static volatile int ffmpeg_exited = 0;
static int main_return_code = 0;

AVFrame * audioFrame = NULL, *audioTempFrame = NULL;
AVFrame * videoFrame = NULL, *videoTempFrame = NULL;

static void
sigterm_handler(int sig)
{
	received_sigterm = sig;
}

static BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
	av_log(NULL, AV_LOG_DEBUG, "\nReceived windows signal %ld\n", fdwCtrlType);

	switch (fdwCtrlType)
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
		sigterm_handler(SIGINT);
		return TRUE;

	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		sigterm_handler(SIGTERM);
		/* Basically, with these 3 events, when we return from this method the
		process is hard terminated, so stall as long as we need to
		to try and let the main thread(s) clean up and gracefully terminate
		(we have at most 5 seconds, but should be done far before that). */
		while (!ffmpeg_exited) {
			Sleep(0);
		}
		return TRUE;

	default:
		av_log(NULL, AV_LOG_ERROR, "Received unknown windows signal %ld\n", fdwCtrlType);
		return FALSE;
	}
}

void term_init(void)
{
	signal(SIGINT, sigterm_handler); /* Interrupt (ANSI).    */
	signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
}

enum AVSampleFormat gInSF = AV_SAMPLE_FMT_S16;
static AVStream* add_audio_stream(AVFormatContext *oc, const AVCodecContext* icct, enum AVCodecID codec_id)
{
	AVCodecContext *c = NULL;
	AVCodec *codec = NULL;
	AVStream *st = NULL;

	st = avformat_new_stream(oc, NULL);
	if (!st) {
		fprintf(stderr, "Could not alloc stream\n");
		exit(1);
	}
	st->id = 1;

	/* find the audio encoder */
	codec = avcodec_find_encoder(codec_id);
	if (!codec) {
		fprintf(stderr, "codec not found\n");
		exit(1);
	}

	c = st->codec;
	avcodec_get_context_defaults3(c, codec);

	c->codec_id = codec_id;
	c->codec_type = AVMEDIA_TYPE_AUDIO;

	/* put sample parameters */
	gInSF = icct->sample_fmt;
	c->sample_fmt = icct->sample_fmt;
	c->bit_rate = 128 * 1024;
	c->sample_rate = icct->sample_rate;
	c->channels = icct->channels;
	c->channel_layout = icct->channel_layout;
	c->frame_size = icct->frame_size;

	// some formats want stream headers to be separate  
	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;

	return st;
}

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
	uint64_t channels,
	int sample_rate, int nb_samples)
{
	AVFrame *frame = av_frame_alloc();
	int ret;

	if (!frame) {
		fprintf(stderr, "Error allocating an audio frame\n");
		exit(1);
	}

	frame->format = sample_fmt;
	frame->channels = channels;
	frame->sample_rate = sample_rate;
	frame->nb_samples = nb_samples;

	if (nb_samples) {
		ret = av_frame_get_buffer(frame, 0);
		if (ret < 0) {
			fprintf(stderr, "Error allocating an audio buffer\n");
			exit(1);
		}
	}

	return frame;
}

struct SwrContext *audioswr_ctx = NULL;

static int open_audio(AVFormatContext *oc, AVStream * audio_st)
{
	AVCodecContext *c;
	int nb_samples;
	AVCodec *codec;
	int ret;
	c = audio_st->codec;

	/* find the video encoder */
	codec = avcodec_find_encoder(c->codec_id);
	if (!codec) {
		fprintf(stderr, "codec not found\n");
		exit(1);
	}

	nb_samples = c->frame_size;
	if (!nb_samples) nb_samples = 1000;

	audioFrame = alloc_audio_frame(c->sample_fmt, c->channels,
		c->sample_rate, nb_samples);

	if (c->sample_fmt != AV_SAMPLE_FMT_S16)
	{
		audioTempFrame = alloc_audio_frame(AV_SAMPLE_FMT_S16, c->channels,
			c->sample_rate, nb_samples);
		/* create resampler context */
		audioswr_ctx = swr_alloc();
		if (!audioswr_ctx) {
			fprintf(stderr, "Could not allocate resampler context\n");
			exit(1);
		}

		/* set options */
		av_opt_set_int(audioswr_ctx, "in_channel_count", c->channels, 0);
		av_opt_set_int(audioswr_ctx, "in_sample_rate", c->sample_rate, 0);
		av_opt_set_sample_fmt(audioswr_ctx, "in_sample_fmt", c->sample_fmt, 0);
		av_opt_set_int(audioswr_ctx, "out_channel_count", c->channels, 0);
		av_opt_set_int(audioswr_ctx, "out_sample_rate", c->sample_rate, 0);
		av_opt_set_sample_fmt(audioswr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

		/* initialize the resampling context */
		if ((ret = swr_init(audioswr_ctx)) < 0) {
			fprintf(stderr, "Failed to initialize the resampling context\n");
			exit(1);
		}
		c->sample_fmt = AV_SAMPLE_FMT_S16;
	}


	/* open the codec */
	if (ret = avcodec_open2(c, codec, NULL) < 0) {
		fprintf(stderr, "could not open codec\n");
		exit(1);
	}

	return 1;
}
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
	AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

/*	printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
		av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
		av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
		av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
		pkt->stream_index);*/
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
	/* rescale output packet timestamp values from codec to stream timebase */
	av_packet_rescale_ts(pkt, *time_base, st->time_base);
	pkt->stream_index = st->index;

	/* Write the compressed frame to the media file. */
	log_packet(fmt_ctx, pkt);
	return av_interleaved_write_frame(fmt_ctx, pkt);
}
#include "libavutil/avassert.h"
static AVFrame *get_audio_frame(AVStream *ost, AVPacket *inpkt,enum AVSampleFormat InSF)
{
	/* check if we want to generate more frames */
	//if (av_compare_ts(ost->next_pts, ost->st->codec->time_base,
	//	STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
	//	return NULL;

	if (inpkt->size <= audioFrame->nb_samples * 4)
	{
		memcpy(audioFrame->data, inpkt->data, audioFrame->nb_samples * 4);
		audioFrame->pts = inpkt->pts;
	}
	else{
		return NULL;
	}

	if (InSF) {
		int dst_nb_samples;
		AVCodecContext *c = ost->codec;
		int ret = 0;
		/* convert samples from native format to destination codec format, using the resampler */
		/* compute destination number of samples */
		dst_nb_samples = av_rescale_rnd(swr_get_delay(audioswr_ctx, c->sample_rate) + audioFrame->nb_samples,
			c->sample_rate, c->sample_rate, AV_ROUND_UP);
		av_assert0(dst_nb_samples == audioFrame->nb_samples);

		/* when we pass a frame to the encoder, it may keep a reference to it
		* internally;
		* make sure we do not overwrite it here
		*/
		ret = av_frame_make_writable(audioFrame);
		if (ret < 0)
			exit(1);

		/* convert to destination format */
		ret = swr_convert(audioswr_ctx,
			audioTempFrame->data, dst_nb_samples,
			(const uint8_t **)audioFrame->data, audioFrame->nb_samples);
		if (ret < 0) {
			fprintf(stderr, "Error while converting\n");
			exit(1);
		}
		audioTempFrame->pts = audioFrame->pts;
		return audioTempFrame;
	}
	else{
		return audioFrame;
	}

	return NULL;
}

static int write_audio_frame(AVFormatContext *oc, AVStream *ost, AVPacket *inpkt, enum AVSampleFormat InSF)
{
	AVCodecContext *c;
	AVPacket pkt = { 0 }; // data and size must be 0;
	AVFrame *frame;
	int ret;
	int got_packet;
	int dst_nb_samples;

	c = ost->codec;

	frame = get_audio_frame(ost, inpkt,InSF);


	ret = avcodec_encode_audio2(c, &pkt, frame, &got_packet);
	if (ret < 0) {
		//fprintf(stderr, "Error encoding audio frame: %s\n", av_err2str(ret));
		exit(1);
	}

	if (got_packet) {
		ret = write_frame(oc, &c->time_base, ost, &pkt);
		if (ret < 0) {
			//fprintf(stderr, "Error while writing audio frame: %s\n",
			//	av_err2str(ret));
			exit(1);
		}
	}

	return (frame || got_packet) ? 0 : 1;
}
struct SwsContext *yuvsws_ctx = NULL;
#define SCALE_FLAGS SWS_BICUBIC
static AVFrame *get_video_frame(AVStream *ost, AVPacket *inpkt,enum AVPixelFormat inputPF)
{
	AVCodecContext *c = ost->codec;

	/* check if we want to generate more frames */
	//if (av_compare_ts(ost->next_pts, ost->st->codec->time_base,
	//	STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
	//	return NULL;

	if (inputPF != AV_PIX_FMT_YUV420P) {
		/* as we only generate a YUV420P picture, we must convert it
		* to the codec pixel format if needed */
		if (yuvsws_ctx) {
			yuvsws_ctx = sws_getContext(c->width, c->height,
				c->pix_fmt,
				c->width, c->height,
				AV_PIX_FMT_YUV420P,
				SCALE_FLAGS, NULL, NULL, NULL);
			if (yuvsws_ctx) {
				fprintf(stderr,
					"Could not initialize the conversion context\n");
				exit(1);
			}
		}
		memcpy(videoFrame->data, inpkt->data, inpkt->size);
		sws_scale(yuvsws_ctx,
			(const uint8_t * const *)videoFrame->data, videoFrame->linesize,
			0, c->height, videoTempFrame->data, videoTempFrame->linesize);
		return videoTempFrame;
	}
	else {
		memcpy(videoFrame->data, inpkt->data, inpkt->size);
		return videoFrame;
	}
	return NULL;
}
/*
* encode one video frame and send it to the muxer
* return 1 when encoding is finished, 0 otherwise
*/
static int write_video_frame(AVFormatContext *oc, AVStream *ost, AVPacket *inpkt,enum AVPixelFormat inPF)
{
	int ret;
	AVCodecContext *c;
	AVFrame *frame;
	int got_packet = 0;

	c = ost->codec;

	frame = get_video_frame(ost,inpkt,inPF);

	if (oc->oformat->flags & AVFMT_RAWPICTURE) {
		/* a hack to avoid data copy with some raw video muxers */
		AVPacket pkt;
		av_init_packet(&pkt);

		if (!frame)
			return 1;

		pkt.flags |= AV_PKT_FLAG_KEY;
		pkt.stream_index = ost->index;
		pkt.data = (uint8_t *)frame;
		pkt.size = sizeof(AVPicture);

		pkt.pts = pkt.dts = frame->pts;
		av_packet_rescale_ts(&pkt, c->time_base, ost->time_base);

		ret = av_interleaved_write_frame(oc, &pkt);
	}
	else {
		AVPacket pkt = { 0 };
		av_init_packet(&pkt);

		/* encode the image */
		ret = avcodec_encode_video2(c, &pkt, frame, &got_packet);
		if (ret < 0) {
			//fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
			exit(1);
		}

		if (got_packet) {
			ret = write_frame(oc, &c->time_base, ost, &pkt);
		}
		else {
			ret = 0;
		}
	}

	if (ret < 0) {
		//fprintf(stderr, "Error while writing video frame: %s\n", av_err2str(ret));
		exit(1);
	}

	return (frame || got_packet) ? 0 : 1;
}

#if 0
static void open_codec(AVFormatContext *oc, AVStream *st)
{
	AVCodec *codec;
	AVCodecContext *c;
	int ret;
	c = st->codec;

	/* find the video encoder */
	codec = avcodec_find_encoder(c->codec_id);
	if (!codec) {
		fprintf(stderr, "codec not found\n");
		exit(1);
	}

	/* open the codec */
	if (ret = avcodec_open2(c, codec, NULL) < 0) {
		fprintf(stderr, "could not open codec\n");
		exit(1);
	}
}
#endif

static void close_codec(AVFormatContext *oc, AVStream *st)
{
	avcodec_close(st->codec);
}

static void open_video(AVFormatContext *oc, AVStream *ost);
static int transcode_init(AVFormatContext *oc)
{
	for (int i = 0; i <oc->nb_streams; i++) {
		AVStream *out_stream = oc->streams[i];
		if (out_stream->codec->codec_type == AVMEDIA_TYPE_VIDEO){
			open_video(oc, out_stream);
		}
		else{
			open_audio(oc, out_stream);
		}
	}
	return 1;
}

static int transcode_close(AVFormatContext *oc)
{
	for (int i = 0; i <oc->nb_streams; i++) {
		AVStream *out_stream = oc->streams[i];
		close_codec(oc, out_stream);
	}
	return 1;
}

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
	AVFrame *picture;
	int ret;

	picture = av_frame_alloc();
	if (!picture)
		return NULL;

	picture->format = pix_fmt;
	picture->width = width;
	picture->height = height;

	/* allocate the buffers for the frame data */
	ret = av_frame_get_buffer(picture, 32);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate frame data.\n");
		exit(1);
	}

	return picture;
}

static void open_video(AVFormatContext *oc,  AVStream *ost)
{
	int ret = 0;
	AVCodecContext *c = ost->codec;
	AVCodec *codec;
	//if (ret < 0) {
		//fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
		//exit(1);
	//}

	/* allocate and init a re-usable frame */
	videoFrame = alloc_picture(c->pix_fmt, c->width, c->height);
	if (!videoFrame) {
		fprintf(stderr, "Could not allocate video frame\n");
		exit(1);
	}

	/* If the output format is not YUV420P, then a temporary YUV420P
	* picture is needed too. It is then converted to the required
	* output format. */
	videoTempFrame = NULL;
	if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
		videoTempFrame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
		if (!videoTempFrame) {
			fprintf(stderr, "Could not allocate temporary picture\n");
			exit(1);
		}
		c->pix_fmt = AV_PIX_FMT_YUV420P;
	}

	/* find the video encoder */
	codec = avcodec_find_encoder(c->codec_id);
	if (!codec) {
		fprintf(stderr, "codec not found\n");
		exit(1);
	}

	/* open the codec */
	if (ret = avcodec_open2(c, codec, NULL) < 0) {
		fprintf(stderr, "could not open codec\n");
		exit(1);
	}
}

enum AVPixelFormat  ginPF= AV_PIX_FMT_YUV420P;
static AVStream *add_video_stream(AVFormatContext *oc, const AVCodecContext * icct, enum AVCodecID codec_id)
{
	AVCodecContext *c = NULL;
	AVStream *st = NULL;
	AVCodec *codec = NULL;

	st = avformat_new_stream(oc, NULL);
	if (!st) {
		fprintf(stderr, "Could not alloc stream\n");
		exit(1);
	}

	c = st->codec;

	/* find the video encoder */
	codec = avcodec_find_encoder(codec_id);
	if (!codec) {
		fprintf(stderr, "codec not found\n");
		exit(1);
	}
	avcodec_get_context_defaults3(c, codec);

	c->codec_id = codec_id;

	/* put sample parameters */
	c->bit_rate = 1000 * 1024;
	/* resolution must be a multiple of two */
	c->width = icct->width;
	c->height = icct->height;
	/* time base: this is the fundamental unit of time (in seconds) in terms
	of which frame timestamps are represented. for fixed-fps content,
	timebase should be 1/framerate and timestamp increments should be
	identically 1. */
	c->time_base.den = icct->time_base.den;
	c->time_base.num = icct->time_base.num;
	c->gop_size = 12; /* emit one intra frame every twelve frames at most */
	c->pix_fmt = icct->pix_fmt;
	ginPF = icct->pix_fmt;
	// some formats want stream headers to be separate  
	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;

	return st;
}
static void printAVDict(AVDictionary *dict)
{
	char *buffer = NULL;

	printf("Testing av_dict_get_string() and av_dict_parse_string()\n");
	av_dict_get_string(dict, &buffer, '=', ',');
	printf("%s\n", buffer);
	av_freep(&buffer);

}

int main(int argc, char* argv[])
{
	AVOutputFormat *ofmt = NULL;
	AVInputFormat  *ifmt = NULL;
	AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
	AVStream* video_st = NULL, *audio_st = NULL;
	AVPacket pkt;
	AVDictionary *dict = NULL;

	int ret, i;
	int videoindex = -1;
	int frame_index = 0;
	int64_t time_start = 0;

	term_init();

	//register codec and flv mux
	av_register_all();
	//register capture device
	avdevice_register_all();
	//Network
	avformat_network_init();

	//Input
	ifmt = av_find_input_format("dshow");
	av_dict_set(&dict, "pix_fmt", "yuv420p", 0);
	printAVDict(dict);
	if ((ret = avformat_open_input(&ifmt_ctx, in_filename, ifmt, &dict)) < 0) {
		printf("Could not open input file.");
		goto end;
	}
	printAVDict(dict);
	if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
		printf("Failed to retrieve input stream information");
		goto end;
	}

	for (i = 0; i<ifmt_ctx->nb_streams; i++)
	if (ifmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO){
		videoindex = i;
		break;
	}
	av_dump_format(ifmt_ctx, 0, in_filename, 0);

	//Output
	avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", out_filename); //RTMP
	if (!ofmt_ctx) {
		printf("Could not create output context\n");
		ret = AVERROR_UNKNOWN;
		goto end;
	}
	ofmt = ofmt_ctx->oformat;
	for (i = 0; i < ifmt_ctx->nb_streams; i++) {
		//Create output AVStream according to input AVStream
		AVStream *in_stream = ifmt_ctx->streams[i];
		if (in_stream->codec->codec_type == AVMEDIA_TYPE_VIDEO){
			video_st = add_video_stream(ofmt_ctx, in_stream->codec, AV_CODEC_ID_H264);
		}
		else if (in_stream->codec->codec_type == AVMEDIA_TYPE_AUDIO){
			audio_st = add_audio_stream(ofmt_ctx, in_stream->codec, AV_CODEC_ID_AAC);
		}
	}
	transcode_init(ofmt_ctx);

	//Dump Format------------------
	av_dump_format(ofmt_ctx, 0, out_filename, 1);

	//Open output URL
	if (!(ofmt->flags & AVFMT_NOFILE)) {
		ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
		if (ret < 0) {
			printf("Could not open output URL '%s'", out_filename);
			goto end;
		}
	}


	//Write file header
	ret = avformat_write_header(ofmt_ctx, NULL);
	if (ret < 0) {
		printf("Error occurred when opening output URL\n");
		goto end;
	}

	time_start = av_gettime_relative();
	while (received_sigterm) {
		int64_t cur_time = av_gettime_relative();

		/* if 'q' pressed, exits */
		//if (check_keyboard_interaction(cur_time) < 0)
		//	break;
		av_init_packet(&pkt);
		ret = av_read_frame(ifmt_ctx, &pkt);
		if (ret < 0)
			break;

		if (pkt.stream_index == videoindex){
			write_video_frame(ofmt_ctx, video_st, &pkt, ginPF);
		}
		else{
			write_audio_frame(ofmt_ctx, audio_st, &pkt,gInSF);
		}

	}
	//Write file trailer
	av_write_trailer(ofmt_ctx);
end:
	avformat_close_input(&ifmt_ctx);
	/* close output */
	if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
		avio_close(ofmt_ctx->pb);
	avformat_free_context(ofmt_ctx);
	if (ret < 0 && ret != AVERROR_EOF) {
		printf("Error occurred.\n");
		return -1;
	}
	return 0;
}



