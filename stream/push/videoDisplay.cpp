#include "stdafx.h"
#include "videoDisplay.h"


videoDisplay::videoDisplay()
{
	reset();
	mTitle = "streampush";
}

void init_sdl()
{
	int flags;
	flags = SDL_INIT_VIDEO | SDL_INIT_TIMER;

	if (SDL_Init(flags)) {
		av_log(NULL, AV_LOG_FATAL, "Could not initialize SDL - %s\n", SDL_GetError());
		av_log(NULL, AV_LOG_FATAL, "(Did you set the DISPLAY variable?)\n");
	}
}

void videoDisplay::video_Screen_Open(int width, int height)
{
	int flags = SDL_SWSURFACE;//SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL;
	int w, h;

	w = width;
	h = height;
	w = FFMIN(16383, w);

	if (mScreen && w == mScreen->w &&mScreen->h == h)
		return;

	mScreen = SDL_SetVideoMode(w, h, 0, flags);
	if (!mScreen) {
		av_log(NULL, AV_LOG_FATAL, "SDL: could not set video mode - exiting\n");
	}

	SDL_WM_SetCaption(mTitle, mTitle);
	return;
}

void videoDisplay::video_Overlay_Open(int width, int height)
{
	if (mBmp && (mBmp->w == width) && (mBmp->h == height))
		return;
	if (mBmp){
		SDL_FreeYUVOverlay(mBmp);
		mBmp = NULL;
	}

	mBmp = SDL_CreateYUVOverlay(width, height,SDL_YV12_OVERLAY,mScreen);
}
void videoDisplay::showVideoFrame(AVFrame * frame)
{
	if (!mInited){
		init_sdl();
		mInited = true;
	}
	video_Screen_Open(frame->width, frame->height);
	video_Overlay_Open(frame->width, frame->height);

	if (mBmp) {
		uint8_t *data[4];
		int linesize[4];

		/* get a pointer on the bitmap */
		SDL_LockYUVOverlay(mBmp);

		data[0] = mBmp->pixels[0];
		data[1] = mBmp->pixels[2];
		data[2] = mBmp->pixels[1];

		linesize[0] = mBmp->pitches[0];
		linesize[1] = mBmp->pitches[2];
		linesize[2] = mBmp->pitches[1];

		// FIXME use direct rendering
		av_image_copy(data, linesize, (const uint8_t **)frame->data, frame->linesize,
			(AVPixelFormat)frame->format, frame->width, frame->height);

		//duplicate_right_border_pixels
		{
			int i, width, height;
			Uint8 *p, *maxp;
			for (i = 0; i < 3; i++) {
				width = mBmp->w;
				height = mBmp->h;
				if (i > 0) {
					width >>= 1;
					height >>= 1;
				}
				if (mBmp->pitches[i] > width) {
					maxp = mBmp->pixels[i] + mBmp->pitches[i] * height - 1;
					for (p = mBmp->pixels[i] + width - 1; p < maxp; p += mBmp->pitches[i])
						*(p + 1) = *p;
				}
			}
		}
		/* update the bitmap content */
		SDL_UnlockYUVOverlay(mBmp);
	}

	if (mBmp) {
		SDL_Rect rect;
		rect.x = rect.y = 0;
		rect.w = mBmp->w;
		rect.h = mBmp->h;
	    SDL_DisplayYUVOverlay(mBmp, &rect);
    }
}

videoDisplay::~videoDisplay()
{
	if (mBmp){
		SDL_FreeYUVOverlay(mBmp);
		mBmp = NULL;
	}
}
