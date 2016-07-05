#pragma once
#include "config.h"

extern "C"{
#include "sdl/SDL.h"
#include "sdl/SDL_thread.h"
}

class videoDisplay
{
public:
	videoDisplay();
	~videoDisplay();
	void showVideoFrame(AVFrame * frame);
private:
	void reset()
	{
		mScreen = NULL;
		mBmp = NULL;
		mTitle = NULL;
		mInited = false;
	}

	void video_Screen_Open(int width, int height);
	void video_Overlay_Open(int width, int height);
	SDL_Surface *mScreen;
	SDL_Overlay *mBmp;
	char *mTitle;
	bool mInited;
};

