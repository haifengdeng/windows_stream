#pragma once
#include <process.h>
#include "config.h"

typedef int (*thread_func)(void*);

void thread_wrapper(void *);
class boost_thread
{
public:	
	boost_thread(thread_func threadfunc,void* param)
	{
		mThreadFunc = threadfunc;
		mParam = param;
		mThread = -1;
		processing = false;
		retval = 0;
		start();
	}

	int start()
	{
		processing = true;
		mThread = _beginthread(boost_thread::thread_wrapper, 0, (void *)this);
		if (mThread == -1){
			TRACE("create thread failed");
			processing = false;
			return -1;
		}
		return 0;
	}

	~boost_thread()
	{
		//join();
	}

	void join()
	{
		while (processing){
			av_usleep(1000);
		}
	}
public:
	void thread_loop()
	{
		processing = true;
		retval = mThreadFunc(mParam);
		processing = false;
	}
static	void thread_wrapper(void * param)
	{
		boost_thread *thread = (boost_thread *)param;
		thread->thread_loop();
		_endthread();
	}
private:
	void* mParam;
	thread_func mThreadFunc;
	uintptr_t  mThread;
	volatile bool processing;
	int retval;

};
