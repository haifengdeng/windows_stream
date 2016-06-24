#ifndef FFPLAY_HEADER_H
#define FFPLAY_HEADER_H

#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <windows.h> 

#include "config.h"
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
#include "pthread/pthread.h"
#include "pthread/sched.h"
#include "pthread/semaphore.h"

#if CONFIG_AVFILTER
# include "libavfilter/avfilter.h"
# include "libavfilter/buffersink.h"
# include "libavfilter/buffersrc.h"
#endif

#include "sdl/SDL.h"
#include "sdl/SDL_thread.h"

#include "cmdutils.h"

#include <assert.h>
#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 25
#define MIN_AUDIO_FRAME 25
#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* Step size for volume control */
#define SDL_VOLUME_STEP (SDL_MIX_MAXVOLUME / 50)

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

#define AV_SYNC_RANGE_MAX  (AV_SYNC_THRESHOLD_MIN*1000000)

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

#define CURSOR_HIDE_DELAY 1000000

typedef struct MyAVPacketList {
	AVPacket pkt;
	struct MyAVPacketList *next;
	int serial;
} MyAVPacketList;

typedef struct PacketQueue {
	MyAVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	int abort_request;
	int serial;
	SDL_mutex *mutex;
	SDL_cond *cond;
} PacketQueue;

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

#define TRANSCODE_FRAME_QUEUE_SIZE 44
typedef struct AudioParams {
	int freq;
	int channels;
	int64_t channel_layout;
	enum AVSampleFormat fmt;
	int frame_size;
	int bytes_per_sec;
} AudioParams;

typedef struct Clock {
	double pts;           /* clock base */
	double pts_drift;     /* clock base minus time at which we updated the clock */
	double last_updated;
	double speed;
	int serial;           /* clock is based on a packet with this serial */
	int paused;
	int *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
} Clock;

/* Common struct for handling all types of decoded data and allocated render buffers. */
typedef struct Frame {
	AVFrame *frame;
	AVSubtitle sub;
	AVSubtitleRect **subrects;  /* rescaled subtitle rectangles in yuva */
	int serial;
	double pts;           /* presentation timestamp for the frame */
	double duration;      /* estimated duration of the frame */
	int64_t pos;          /* byte position of the frame in the input file */
	SDL_Overlay *bmp;
	int allocated;
	int reallocate;
	int width;
	int height;
	AVRational sar;
} Frame;

typedef struct FrameQueue {
	Frame queue[FRAME_QUEUE_SIZE];
	int rindex;
	int windex;
	int size;
	int max_size;
	int keep_last;
	int rindex_shown;
	SDL_mutex *mutex;
	SDL_cond *cond;
	PacketQueue *pktq;
} FrameQueue;

enum {
	AV_SYNC_AUDIO_MASTER, /* default choice */
	AV_SYNC_VIDEO_MASTER,
	AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

typedef struct Decoder {
	AVPacket pkt;
	AVPacket pkt_temp;
	PacketQueue *queue;
	AVCodecContext *avctx;
	int pkt_serial;
	int finished;
	int packet_pending;
	SDL_cond *empty_queue_cond;
	int64_t start_pts;
	AVRational start_pts_tb;
	int64_t next_pts;
	AVRational next_pts_tb;
	SDL_Thread *decoder_tid;
} Decoder;



struct OutputFile;
typedef struct VideoState {
	SDL_Thread *read_tid;
	AVInputFormat *iformat;
	int abort_request;
	int force_refresh;
	int paused;
	int last_paused;
	int queue_attachments_req;
	int seek_req;
	int seek_flags;
	int64_t seek_pos;
	int64_t seek_rel;
	int read_pause_return;
	AVFormatContext *ic;
	int realtime;

	Clock audclk;
	Clock vidclk;
	Clock extclk;

	FrameQueue pictq;
	FrameQueue subpq;
	FrameQueue sampq;

	Decoder auddec;
	Decoder viddec;
	Decoder subdec;

	int viddec_width;
	int viddec_height;

	int audio_stream;

	int av_sync_type;

	double audio_clock;
	int audio_clock_serial;
	double audio_diff_cum; /* used for AV difference average computation */
	double audio_diff_avg_coef;
	double audio_diff_threshold;
	int audio_diff_avg_count;
	AVStream *audio_st;
	PacketQueue audioq;
	int audio_hw_buf_size;
	uint8_t silence_buf[SDL_AUDIO_MIN_BUFFER_SIZE];
	uint8_t *audio_buf;
	uint8_t *audio_buf1;
	unsigned int audio_buf_size; /* in bytes */
	unsigned int audio_buf1_size;
	int audio_buf_index; /* in bytes */
	int audio_write_buf_size;
	int audio_volume;
	int muted;
	struct AudioParams audio_src;
#if CONFIG_AVFILTER
	struct AudioParams audio_filter_src;
#endif
	struct AudioParams audio_tgt;
	struct SwrContext *swr_ctx;
	int frame_drops_early;
	int frame_drops_late;

	enum ShowMode {
		SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
	} show_mode;
	int16_t sample_array[SAMPLE_ARRAY_SIZE];
	int sample_array_index;
	RDFTContext *rdft;
	int rdft_bits;
	FFTSample *rdft_data;
	int xpos;
	double last_vis_time;

	int subtitle_stream;
	AVStream *subtitle_st;
	PacketQueue subtitleq;

	double frame_timer;
	double frame_last_returned_time;
	double frame_last_filter_delay;
	int video_stream;
	AVStream *video_st;
	PacketQueue videoq;
	double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
#if !CONFIG_AVFILTER
	struct SwsContext *img_convert_ctx;
#endif
	struct SwsContext *sub_convert_ctx;
	SDL_Rect last_display_rect;
	int eof;

	char *filename;
	int width, height, xleft, ytop;
	int step;



	int last_video_stream, last_audio_stream, last_subtitle_stream;

	SDL_cond *continue_read_thread;

	struct OutputFile   **output_files ;
	int         nb_output_files ;
	struct OptionsContext * optctx;
	int btranscode;
	int btranscode_exit;
	struct TranscodeFrameQueue  audioQueue;
	struct TranscodeFrameQueue  videoQueue;
	SDL_Thread * transcode_tid;
	SDL_cond *transcode_singal;
	SDL_mutex *transcode_mutex;
} VideoState;

typedef struct OptionsContext {
	//file
	char *filename;
	char *format;     //like flv format file

	int video_disable;
	int audio_disable;

	int64_t start_time;
    char *codecTag;

	//audio
	enum AVCodecID a_codec_id;
	int audio_channels;
	enum AVSampleFormat sample_fmt;
	int sample_rate;
	int a_bitrate;
	AVRational a_dectb;
	AVRational a_pkttb;


	//video
	enum AVCodecID v_codec_id;
	AVRational frame_rate;
	AVRational frame_aspect_ratio;
	int width;
	int height;
	enum AVPixelFormat pix_fmt;
	int v_bitrate;
	AVRational v_dectb;
	AVRational v_pkttb;

	int64_t recording_time;
	int64_t stop_time;
	uint64_t limit_filesize;
	float mux_preload;
	float mux_max_delay;
	int shortest;

	double qscale;
	AVDictionary *codec_opts;
	AVDictionary *format_opts;
	AVDictionary *resample_opts;
	AVDictionary *sws_dict;
	AVDictionary *swr_opts;
}OptionsContext;

int transcode_thread(void *arg);
struct OutputStream;
typedef int bool;
typedef struct OutputFile {
	AVFormatContext *ctx;
	VideoState * is;
	AVDictionary *opts;
	int ost_index;       /* index of the first stream in output_streams */
	int64_t recording_time;  ///< desired length of the resulting file in microseconds == AV_TIME_BASE units
	int64_t start_time;      ///< start time in microseconds == AV_TIME_BASE units
	uint64_t limit_filesize; /* filesize limit expressed in bytes */

	char *filename;

	int shortest;
	struct OutputStream **output_streams;
	int         nb_output_streams;

	int64_t master_pts;//以AV_TIME_BASE为单位
	int64_t master_time;//决定下一次写入的时间
	int master_inited;
	int64_t ts_offset; //with AV_TIME_BASE units;
	bool  abort;
} OutputFile;
#define SCALE_FLAGS SWS_BICUBIC

typedef struct OutputFilter {
	AVFilterContext     *filter;
	struct OutputStream *ost;
	struct FilterGraph  *graph;
	uint8_t             *name;

	/* temporary storage until stream maps are processed */
	AVFilterInOut       *out_tmp;
	enum AVMediaType     type;
} OutputFilter;

typedef struct OutputStream{
	int index;               /* stream index in the output file */
	AVStream *st;
	OutputFile * file;
	enum AVMediaType type;
	int encoding_needed;
	int frame_number;

	//AVBitStreamFilterContext *bitstream_filters;
	AVCodecContext *enc_ctx;
	AVCodec *enc;
	int64_t max_frames;
	AVFrame *filtered_frame;
	AVFrame *last_frame;
	enum AVCodecID a_codecId;

	TranscodeFrameQueue *inputFrame;
	AVRational tb_decinput;
	AVRational tb_pktinput;

	int64_t next_pts;

	int64_t first_pts;//base of tb_pktinput
	int64_t last_mux_dts;	/* dts of the last packet sent to the muxer */

	OutputFilter *filter;

	/* video only */
	AVRational frame_rate;
	int is_cfr;
	int force_fps;
	int top_field_first;
	int rotate_overridden;
	AVRational frame_aspect_ratio;
	enum AVPixelFormat src_pix_fmt;
	enum AVPixelFormat dst_pix_fmt;
	struct SwsContext *videosws_ctx;
	AVFrame *v_tempFrame;
	int64_t sync_opts;
	enum AVCodecID b_codecId;
	int width;
	int height;



	/* audio only */
	int *audio_channels_map;             /* list of the channels id to pick from the source stream */
	int audio_channels_mapped;           /* number of channels in audio_channels_map */
	enum AVSampleFormat src_sample_fmt;
	enum AVSampleFormat dst_sample_fmt;
	struct SwrContext *audioswr_ctx;
	AVFrame*a_tempFrame;

	AVDictionary *codec_opts;
	AVDictionary *format_opts;
	AVDictionary *resample_opts;
	AVDictionary *sws_dict;
	AVDictionary *swr_opts;

	int unavailable;                     /* true if the steram is unavailable (possibly temporarily) */
	int keep_pix_fmt;

	AVCodecParserContext *parser;

	/* stats */
	// combined size of all the packets written
	uint64_t data_size;
	// number of packets send to the muxer
	uint64_t packets_written;
	// number of frames/samples sent to the encoder
	uint64_t frames_encoded;
	uint64_t samples_encoded;

	/* packet quality factor */
	int quality;

	/* packet picture type */
	int pict_type;

	/* frame encode sum of squared error values */
	int64_t error[4];
}OutputStream;

struct ffmpeg_data{
	AVStream           *video;
	AVStream           *audio;
	AVCodec            *acodec;
	AVCodec            *vcodec;
	AVFormatContext    *output;

	AVCodecContext *audioctx;
	AVCodecContext *videoctx;

};
typedef struct ffmpeg_data ffmpeg_data_t;
int tframe_queue_nb_remaining(TranscodeFrameQueue *f);
Frame *tframe_queue_peek_readable(TranscodeFrameQueue *f);
Frame *tframe_queue_peek_writable(TranscodeFrameQueue *f);
int tframe_queue_init(TranscodeFrameQueue *f, int max_size);
void tframe_queue_push(TranscodeFrameQueue *f);
void tframe_abort(TranscodeFrameQueue*f);
void tframe_queue_destory(TranscodeFrameQueue *f);


void transcode_abort(VideoState *is);

#define snprintf _snprintf

#endif //FFPLAY_HEADER_H
