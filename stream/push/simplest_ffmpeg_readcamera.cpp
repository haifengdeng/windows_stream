/**
 * ��򵥵Ļ���FFmpeg��AVDevice���ӣ���ȡ����ͷ��
 * Simplest FFmpeg Device (Read Camera)
 *
 * ������ Lei Xiaohua
 * leixiaohua1020@126.com
 * �й���ý��ѧ/���ֵ��Ӽ���
 * Communication University of China / Digital TV Technology
 * http://blog.csdn.net/leixiaohua1020
 *
 * ������ʵ���˱�������ͷ���ݵĻ�ȡ�������ʾ���ǻ���FFmpeg
 * ��libavdevice�����򵥵����ӡ�ͨ�������ӣ�����ѧϰFFmpeg��
 * libavdevice����ʹ�÷�����
 * ��������Windows�¿���ʹ��2�ַ�ʽ��ȡ����ͷ���ݣ�
 *  1.VFW: Video for Windows ��Ļ��׽�豸��ע������URL���豸����ţ�
 *          ��0��9��
 *  2.dshow: ʹ��Directshow��ע�����߻����ϵ�����ͷ�豸������
 *         ��Integrated Camera����ʹ�õ�ʱ����Ҫ�ĳ��Լ�����������ͷ��
 *          �������ơ�
 * ��Linux�������ʹ��video4linux2��ȡ����ͷ�豸��
 *
 * This software read data from Computer's Camera and play it.
 * It's the simplest example about usage of FFmpeg's libavdevice Library. 
 * It's suiltable for the beginner of FFmpeg.
 * This software support 2 methods to read camera in Microsoft Windows:
 *  1.gdigrab: VfW (Video for Windows) capture input device.
 *             The filename passed as input is the capture driver number,
 *             ranging from 0 to 9.
 *  2.dshow: Use Directshow. Camera's name in author's computer is 
 *             "Integrated Camera".
 * It use video4linux2 to read Camera in Linux.
 * 
 */


#include "stdafx.h"

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
#include "libswresample/swresample.h"
	//SDL
#include "sdl/SDL.h"
#include "sdl/SDL_thread.h"
};

//Output YUV420P 
#define OUTPUT_YUV420P 0
//'1' Use Dshow 
//'0' Use VFW
#define USE_DSHOW 1

//Show Device
void show_dshow_device(){
	AVFormatContext *pFormatCtx = avformat_alloc_context();
	AVDictionary* options = NULL;
	av_dict_set(&options,"list_devices","true",0);
	AVInputFormat *iformat = av_find_input_format("dshow");
	printf("Device Info=============\n");
	avformat_open_input(&pFormatCtx,"video=dummy",iformat,&options);
	printf("========================\n");
}

//Show Device Option
void show_dshow_device_option(){
	AVFormatContext *pFormatCtx = avformat_alloc_context();
	AVDictionary* options = NULL;
	av_dict_set(&options,"list_options","true",0);
	AVInputFormat *iformat = av_find_input_format("dshow");
	printf("Device Option Info======\n");
	avformat_open_input(&pFormatCtx,"video=Integrated Camera",iformat,&options);
	printf("========================\n");
}

//Show VFW Device
void show_vfw_device(){
	AVFormatContext *pFormatCtx = avformat_alloc_context();
	AVInputFormat *iformat = av_find_input_format("vfwcap");
	printf("VFW Device Info======\n");
	avformat_open_input(&pFormatCtx,"list",iformat,NULL);
	printf("=====================\n");
}

static  Uint8  *audio_chunk;
static  Uint32  audio_len;
static  Uint8  *audio_pos;
//-----------------
/*  The audio function callback takes the following parameters:
stream: A pointer to the audio buffer to be filled
len: The length (in bytes) of the audio buffer (���ǹ̶���4096��)
�ص�����
ע�⣺mp3Ϊʲô���Ų�˳����
len=4096;audio_len=4608;�������512��Ϊ����512�������ٵ���һ�λص�����������
m4a,aac�Ͳ����ڴ�����(����4096)��
*/
void  fill_audio(void *udata, Uint8 *stream, int len){
	/*  Only  play  if  we  have  data  left  */
	if (audio_len == 0)
		return;
	/*  Mix  as  much  data  as  possible  */
	len = (len>audio_len ? audio_len : len);
	SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
	audio_pos += len;
	audio_len -= len;
}
//-----------------
#include <windows.h>
#include <Mmsystem.h>
#define _WAVE_
#if 0
int main(int argc, char* argv[])
{

	AVFormatContext	*pFormatCtx;
	int				i, videoindex = -1,audioindex =-1;
	AVCodecContext	*pCodecCtx,*pAudioCodecCtx;
	AVCodec			*pCodec,*pAudioCodec;
	
	av_register_all();
	avformat_network_init();
	pFormatCtx = avformat_alloc_context();
	
	//Open File
	//char filepath[]="src01_480x272_22.h265";
	//avformat_open_input(&pFormatCtx,filepath,NULL,NULL)

	//Register Device
	avdevice_register_all();
	//Show Dshow Device
	show_dshow_device();
	//Show Device Options
	show_dshow_device_option();
	//Show VFW Options
	show_vfw_device();
//Windows
#ifdef _WIN32
#if USE_DSHOW
	AVInputFormat *ifmt=av_find_input_format("dshow");
	//Set own video device's name
	if (avformat_open_input(&pFormatCtx, "video=Integrated Camera:audio=External Mic (Realtek High Defi", ifmt, NULL) != 0){
		printf("Couldn't open input stream.���޷�����������\n");
		return -1;
	}
#else
	AVInputFormat *ifmt=av_find_input_format("vfwcap");
	if(avformat_open_input(&pFormatCtx,"0",ifmt,NULL)!=0){
		printf("Couldn't open input stream.���޷�����������\n");
		return -1;
	}
#endif
#endif
//Linux
#ifdef linux
	AVInputFormat *ifmt=av_find_input_format("video4linux2");
	if(avformat_open_input(&pFormatCtx,"/dev/video0",ifmt,NULL)!=0){
		printf("Couldn't open input stream.���޷�����������\n");
		return -1;
	}
#endif


	if(avformat_find_stream_info(pFormatCtx,NULL)<0)
	{
		printf("Couldn't find stream information.���޷���ȡ����Ϣ��\n");
		return -1;
	}
	videoindex=audioindex=-1;
	for (i = 0; i < pFormatCtx->nb_streams; i++){
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoindex = i;
		}
		else if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO){
			audioindex = i;
		}
	}
	if(videoindex==-1 || audioindex == -1)
	{
		printf("Couldn't find a audio/video stream.��û���ҵ���Ƶ����Ƶ����\n");
		return -1;
	}
	pCodecCtx=pFormatCtx->streams[videoindex]->codec;
	pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
	if(pCodec==NULL)
	{
		printf("Codec not found.��û���ҵ���������\n");
		return -1;
	}
	if(avcodec_open2(pCodecCtx, pCodec,NULL)<0)
	{
		printf("Could not open codec.���޷��򿪽�������\n");
		return -1;
	}

	pAudioCodecCtx = pFormatCtx->streams[audioindex]->codec;
	pAudioCodec = avcodec_find_decoder(pAudioCodecCtx->codec_id);
	if (pAudioCodec == NULL)
	{
		printf("Codec not found.��û���ҵ���Ƶ��������\n");
		return -1;
	}
	if (avcodec_open2(pAudioCodecCtx, pAudioCodec, NULL)<0)
	{
		printf("Could not open codec.���޷�����Ƶ��������\n");
		return -1;
	}

	FILE *pFile;
#ifdef _WAVE_
	pFile = fopen("D:/output.wav", "wb");
	fseek(pFile, 44, SEEK_SET); //Ԥ���ļ�ͷ��λ��
#else
	pFile = fopen("D:/output.pcm", "wb");
#endif

	AVFrame	*pAudioFrame;
	pAudioFrame = avcodec_alloc_frame();
	//�����Ƶ���ݴ�С��һ��С������ڴ档
	int out_linesize;
	//����ڴ��С
	int out_buffer_size = av_samples_get_buffer_size(&out_linesize, pAudioCodecCtx->channels, pAudioCodecCtx->frame_size, pAudioCodecCtx->sample_fmt, 1);
	if (out_buffer_size <= 0)
	{
		out_buffer_size = av_samples_get_buffer_size(&out_linesize, pAudioCodecCtx->channels,1000, pAudioCodecCtx->sample_fmt, 1);
		out_linesize = 88200;
		out_buffer_size = out_linesize * 2;
	}
	uint8_t *out_Audiobuffer = new uint8_t[out_buffer_size];


	AVFrame	*pFrame,*pFrameYUV;
	pFrame=avcodec_alloc_frame();
	pFrameYUV=avcodec_alloc_frame();
	uint8_t *out_buffer=(uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
	//SDL----------------------------
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {  
		printf( "Could not initialize SDL - %s\n", SDL_GetError()); 
		return -1;
	} 
	SDL_EventState(SDL_ACTIVEEVENT, SDL_IGNORE);
	SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
	SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

	//�ṹ�壬����PCM���ݵ������Ϣ
	SDL_AudioSpec wanted_spec;
	wanted_spec.freq = pAudioCodecCtx->sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = pAudioCodecCtx->channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = 1024; //����AAC��M4a���������Ĵ�С
	//wanted_spec.samples = 1152; //����MP3��WMAʱ����
	wanted_spec.callback = fill_audio;
	wanted_spec.userdata = pAudioCodecCtx;


	if (SDL_OpenAudio(&wanted_spec, NULL)<0)//���裨2������Ƶ�豸 
	{
		printf("can't open audio.\n");
		return 0;
	}
	//-----------------------------------------------------
	printf("Bitrate:\t %3d\n", pFormatCtx->bit_rate);
	printf("Decoder Name:\t %s\n", pAudioCodecCtx->codec->long_name);
	printf("Channels:\t %d\n", pAudioCodecCtx->channels);
	printf("Sample per Second\t %d \n", pAudioCodecCtx->sample_rate);

	int screen_w=0,screen_h=0;
	SDL_Surface *screen; 
	screen_w = pCodecCtx->width;
	screen_h = pCodecCtx->height;
	screen = SDL_SetVideoMode(screen_w, screen_h, 0,0);

	if(!screen) {  
		printf("SDL: could not set video mode - exiting:%s\n",SDL_GetError());  
		return -1;
	}
	SDL_Overlay *bmp; 
	bmp = SDL_CreateYUVOverlay(pCodecCtx->width, pCodecCtx->height,SDL_YV12_OVERLAY, screen); 
	SDL_Rect rect;
	//SDL End------------------------
	int ret, got_picture;

	AVPacket *packet=(AVPacket *)av_malloc(sizeof(AVPacket));
	//Output Information-----------------------------
	printf("File Information���ļ���Ϣ��---------------------\n");
	av_dump_format(pFormatCtx,0,NULL,0);
	printf("-------------------------------------------------\n");

#if OUTPUT_YUV420P 
    FILE *fp_yuv=fopen("output.yuv","wb+");  
#endif  

	struct SwrContext *au_convert_ctx;
	au_convert_ctx = swr_alloc();
	au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, 44100,
		AV_CH_LAYOUT_STEREO, pAudioCodecCtx->sample_fmt, pAudioCodecCtx->sample_rate, 0, NULL);
	swr_init(au_convert_ctx);


	struct SwsContext *img_convert_ctx;
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL); 
	//------------------------------

	int index = 0;
	uint32_t len = 0;

	while(av_read_frame(pFormatCtx, packet)>=0)
	{
		if(packet->stream_index==videoindex)
		{
			ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
			if(ret < 0)
			{
				printf("Decode Error.���������\n");
				return -1;
			}
			if(got_picture)
			{
				sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
				
#if OUTPUT_YUV420P
				int y_size=pCodecCtx->width*pCodecCtx->height;  
				fwrite(pFrameYUV->data[0],1,y_size,fp_yuv);    //Y 
				fwrite(pFrameYUV->data[1],1,y_size/4,fp_yuv);  //U
				fwrite(pFrameYUV->data[2],1,y_size/4,fp_yuv);  //V
#endif
				SDL_LockYUVOverlay(bmp);
				bmp->pixels[0]=pFrameYUV->data[0];
				bmp->pixels[2]=pFrameYUV->data[1];
				bmp->pixels[1]=pFrameYUV->data[2];     
				bmp->pitches[0]=pFrameYUV->linesize[0];
				bmp->pitches[2]=pFrameYUV->linesize[1];   
				bmp->pitches[1]=pFrameYUV->linesize[2];
				SDL_UnlockYUVOverlay(bmp); 
				rect.x = 0;    
				rect.y = 0;    
				rect.w = screen_w;    
				rect.h = screen_h;  
				SDL_DisplayYUVOverlay(bmp, &rect); 
				//Delay 40ms
				//SDL_Delay(40);
			}
		}
		else if (packet->stream_index == audioindex){
			ret = avcodec_decode_audio4(pAudioCodecCtx, pAudioFrame, &got_picture, packet);
			if (ret < 0)
			{
				printf("Error in decoding audio frame.\n");
				exit(0);
			}
			if (got_picture > 0)
			{
#if 1
				fwrite(pAudioFrame->data[0], 1, pAudioFrame->nb_samples*2, pFile);
				len += pAudioFrame->nb_samples * 2;
#endif
#if 1
				swr_convert(au_convert_ctx, &out_Audiobuffer, out_linesize, (const uint8_t **)pAudioFrame->data, pAudioFrame->nb_samples);

				printf("index:%5d\t pts %5d\n", index, packet->pts);
#endif
				//ֱ��д��
				index++;
				if (index == 1000)
					break;
			}
#if 1
			//---------------------------------------
			//printf("begin....\n"); 
			//������Ƶ���ݻ���,PCM����
			audio_chunk = (Uint8 *)out_Audiobuffer;
			//������Ƶ���ݳ���
			audio_len = out_linesize;
			//audio_len = 4096;
			//����mp3��ʱ���Ϊaudio_len = 4096
			//���Ƚ���������������������MP3һ֡����4608
			//ʹ��һ�λص�������4096�ֽڻ��壩���Ų��꣬���Ի�Ҫʹ��һ�λص����������²��Ż���������
			//���ó�ʼ����λ��
			audio_pos = audio_chunk;
			//�ط���Ƶ���� 
			SDL_PauseAudio(0);
			//printf("don't close, audio playing...\n"); 
			//while (audio_len>0)//�ȴ�ֱ����Ƶ���ݲ������! 
			//	SDL_Delay(1);
			//---------------------------------------
#endif
		}
		av_free_packet(packet);
	}
	sws_freeContext(img_convert_ctx);

#if OUTPUT_YUV420P 
    fclose(fp_yuv);
#endif 

#ifdef _WAVE_
	typedef unsigned char  uint8_t;
	typedef unsigned short uint16_t;
	typedef unsigned int  uint32_t;
	 struct wav_header {
		uint32_t riff_id;
		uint32_t riff_sz;
		uint32_t riff_fmt;
		uint32_t fmt_id;
		uint32_t fmt_sz;
		uint16_t audio_format;
		uint16_t num_channels;
		uint32_t sample_rate;
		uint32_t byte_rate;
		uint16_t block_align;
		uint16_t bits_per_sample;
		uint32_t data_id;
		uint32_t data_sz;
	} wh;
	fseek(pFile, 0, SEEK_SET);

	memcpy(&wh.riff_id, "RIFF", 4);
	wh.riff_sz = 36 + len;
	memcpy(&wh.riff_fmt, "WAVE", 4);

	memcpy(&wh.fmt_id, "fmt ", 4);
	wh.fmt_sz = 16;
	wh.audio_format = 1;
	wh.num_channels = 1;//pAudioCodecCtx->channels;
	wh.sample_rate = pAudioCodecCtx->sample_rate;
	wh.bits_per_sample = 16;
	wh.byte_rate = (wh.bits_per_sample / 8) * wh.num_channels * wh.sample_rate;
	wh.block_align = wh.num_channels * (wh.bits_per_sample / 8);

	memcpy(&wh.data_id, "data", 4);
	wh.data_sz = len;

	fwrite(&wh, 1, sizeof(wh), pFile);
	fclose(pFile);
#endif
	SDL_Quit();

	av_free(out_buffer);
	av_free(out_Audiobuffer);
	av_free(pFrameYUV);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);

	return 0;
}
#endif
