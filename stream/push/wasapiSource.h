#pragma once
#include "stream_source.h"

#include "wasapi/enum-wasapi.hpp"

#include "wasapi/HRError.hpp"
#include "wasapi/ComPtr.hpp"
#include "wasapi/WinHandle.hpp"
#include "wasapi/CoTaskMemPtr.hpp"

#define KSAUDIO_SPEAKER_4POINT1 (KSAUDIO_SPEAKER_QUAD|SPEAKER_LOW_FREQUENCY)
#define KSAUDIO_SPEAKER_2POINT1 (KSAUDIO_SPEAKER_STEREO|SPEAKER_LOW_FREQUENCY)

#define OPT_DEVICE_ID         "device_id"
#define OPT_USE_DEVICE_TIMING "use_device_timing"


class wasapi_callback{
public:
	virtual void raw_audio(wasapiSourceData & data) = 0;
};

class WASAPISource {
public:
	ComPtr<IMMDevice>           device;
	ComPtr<IAudioClient>        client;
	ComPtr<IAudioCaptureClient> capture;
	ComPtr<IAudioRenderClient>  render;

	std::string                      device_id;
	std::string                      device_name;
	bool                        isInputDevice;
	bool                        useDeviceTiming = false;
	bool                        isDefaultDevice = false;

	bool                        reconnecting = false;
	bool                        previouslyFailed = false;
	WinHandle                   reconnectThread;

	bool                        active = false;
	WinHandle                   captureThread;

	WinHandle                   stopSignal;
	WinHandle                   receiveSignal;

	speaker_layout              speakers;
	AVSampleFormat                format;
	uint32_t                    sampleRate;
	wasapi_callback *           mCallback;

	static DWORD WINAPI ReconnectThread(LPVOID param);
	static DWORD WINAPI CaptureThread(LPVOID param);

	bool ProcessCaptureData();

	inline void Start();
	inline void Stop();
	void Reconnect();

	bool InitDevice(IMMDeviceEnumerator *enumerator);
	void InitName();
	void InitClient();
	void InitRender();
	void InitFormat(WAVEFORMATEX *wfex);
	void InitCapture();
	void Initialize();

	bool TryInitialize();

	void UpdateSettings(Json::Value &settings);
public:
	WASAPISource(Json::Value &settings, wasapi_callback* callback, bool input);
	virtual ~WASAPISource();

	void Update(Json::Value &settings);
};



struct waspiOutData{
	int dst_bufsize;
	int line_size;
	int channels;
	int nb_samples;
	int start_frame;
	AVSampleFormat fmt;
	uint8_t ** data;
	int64_t timestamp;
};
class wasapiSourceCallback;
class wasapiSource :
	public stream_source
{
public:
	static void getSourceDefaultSetting(Json::Value &setting);
public:
	wasapiSource(Json::Value& setting,bool input);
	~wasapiSource();
	void raw_audio(wasapiSourceData & AudioData);
	bool selectWasapiData(int & writtenSamples, int64_t start, int64_t end);
	//virtual
	virtual void getSourceStringId(std::string & id);
	virtual int getSourceType();
	virtual int getSourceFlag();

	//
	virtual int activeSourceStream();
	virtual int deactiveSourceStream();
	virtual int updateSourceSetting(Json::Value & info);
	virtual bool isactive();

	//
	virtual void getSourceProperties(Json::Value &props, propertytype flag);
	//
	virtual bool getSourceFrame(AVFrame* frame, bool audio);
	virtual AVFrame * getSourceFrame(bool audio);

	virtual void  rawRender(AVFrame *frame);
private:
	WASAPISource* mSource;
	Json::Value mSetting;
	bool mInput;
	bool mActive;
	wasapiSourceCallback *mCallback;

	//store resampled data	
	std::mutex mMutex;
	std::queue<waspiOutData> mAudioFrameQueue;
	int mSamples;

	//store 1024 frames for mixer
	AVFrame * mMixerFrame;
};

class wasapiMonitor
{
public:
	wasapiMonitor();
	~wasapiMonitor();
	void choose_wasapiDevice(Json::Value & settings);
private:
	std::vector<AudioDeviceInfo> mDevices;
};
