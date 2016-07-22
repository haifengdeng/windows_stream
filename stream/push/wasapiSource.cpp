#include "wasapiSource.h"
#include "audio-io.h"
//default device
static void GetWASAPIDefaultsInput(Json::Value & settings)
{
	settings[OPT_DEVICE_ID] = "default";
	settings[OPT_USE_DEVICE_TIMING] = false;
}

static void GetWASAPIDefaultsOutput(Json::Value & settings)
{
	settings[OPT_DEVICE_ID] = "default";
	settings[OPT_USE_DEVICE_TIMING] = true;
}


//monitor
wasapiMonitor::wasapiMonitor()
{
	CoInitialize(0);
	GetWASAPIAudioDevices(mDevices, false);
}

wasapiMonitor::~wasapiMonitor()
{
	CoUninitialize();
}

void wasapiMonitor::choose_wasapiDevice(Json::Value & settings)
{
	int input = 0;
	std::map<int, std::string> mapDevice;
	mapDevice[0] = "default";
	for (int i = 0; i < mDevices.size(); i++){
		mapDevice[i + 1] = mDevices[i].id;
	}
DeviceSelect:
	printf("please choose the wasapi outDevice\n");
	for (int i = 0; i < mapDevice.size(); i++)
	{
		printf("  %d:%s\n", i, mapDevice[i].c_str());
	}
	scanf("%d", &input);
	if (input < 0 || input >= mapDevice.size()){
		printf("invalid wasapi audio device,please reselect:\n");
		goto DeviceSelect;
	}

	settings[OPT_DEVICE_ID] = mapDevice[input];
	settings[OPT_USE_DEVICE_TIMING] = true;
}


//calback

class wasapiSourceCallback:
	public wasapi_callback{
public:
	wasapiSourceCallback(wasapiSource *source){ mSource = source; }
	virtual void raw_audio(wasapiSourceData & data){ return mSource->raw_audio(data); }
private:
	wasapiSource *mSource;
};
/*wasapiSource*/
static WASAPISource *CreateWASAPISource(Json::Value & settings, wasapiSourceCallback *callback, bool input)
{
	try {
		return new WASAPISource(settings, callback,input);
	}
	catch (const char *error) {
		TRACE("[CreateWASAPISource] %s", error);
	}

	return nullptr;
}

static WASAPISource *CreateWASAPIInput(Json::Value & settings, wasapiSourceCallback *callback)
{
	return CreateWASAPISource(settings, callback, true);
}

static WASAPISource *CreateWASAPIOutput(Json::Value & settings, wasapiSourceCallback *callback)
{
	return CreateWASAPISource(settings, callback, false);
}

static void DestroyWASAPISource(WASAPISource *obj)
{
	delete static_cast<WASAPISource*>(obj);
}

static void UpdateWASAPISource(WASAPISource *obj, Json::Value & settings)
{
	static_cast<WASAPISource*>(obj)->Update(settings);
}

AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
	uint64_t channels,
	int sample_rate, int nb_samples);

wasapiSource::wasapiSource(Json::Value& setting, bool input)
{
	mSetting = setting;
	mInput = input;
	mSource = NULL;
	mActive = false;
	mSamples = 0;
	mAudioResampler = NULL;
	mTempData = NULL;
	mMixerFrame = alloc_audio_frame(mOutSampleFmt, 2, 44100, 1024);
	memset(&mResample_info, 0, sizeof(struct resample_info));
	CoInitialize(0);
}

wasapiSource::~wasapiSource()
{
	if (mSource){
		DestroyWASAPISource(mSource);
		mSource = NULL;
	}
	mActive = false;
	CoUninitialize();
}


void wasapiSource::getSourceStringId(std::string & id)
{
	id = "wasapi Source";
}
int wasapiSource::getSourceType()
{
	return SOURCE_TYPE_INPUT;
}
int wasapiSource::getSourceFlag()
{
	return SOURCE_AUDIO;
}

int wasapiSource::activeSourceStream()
{
	if (mCallback)
		mCallback = new wasapiSourceCallback(this);
	mSource = CreateWASAPIOutput(mSetting, mCallback);
	if (mSource)
		mActive = true;
	return 0;
}
int wasapiSource::deactiveSourceStream()
{
	mActive = false;
	if (mSource){
		DestroyWASAPISource(mSource);
		mSource = NULL;
	}
	return 0;
}
int wasapiSource::updateSourceSetting(Json::Value & info)
{
	mSetting = info;
	UpdateWASAPISource(mSource, mSetting);
	return 0;
}
bool wasapiSource::isactive()
{
	return mActive;
}

void wasapiSource::getSourceProperties(Json::Value &props, propertytype flag)
{
	if (flag & PROPERTYTYPE_AUDIO && mSource){
		props["audioSampleRate"] = mSource->sampleRate;
		props["audioChannel"] = (mSource->speakers == SPEAKERS_STEREO) ? 2 : 1;
		props["audioSampleFmt"] = mSource->format;
	}
}


void wasapiSource::raw_audio(wasapiSourceData & audioData)
{
	
	process_audio(audioData);

	source_output_audio_data();
}

bool wasapiSource::selectWasapiData(int & writtenSamples,int64_t start, int64_t end)
{
	int nb_samples = 0;
    waspiOutData wasApiData;
	int start_pos = 0;
	int64_t _start = 0, _duration = 0, _end = 0;
	int64_t avalid_start = 0, avalid_end = 0;
	int frames_avai = 0;
	bool again = false;
	//find Start pos
	while (mAudioFrameQueue.size()){
		
FoundStartPos:
		mMutex.lock();
		wasApiData= mAudioFrameQueue.front();
		frames_avai = (wasApiData.nb_samples - wasApiData.start_frame);
		mMutex.unlock();

		_start = wasApiData.timestamp;
		_duration = frames_avai * 1000000 / 44100;
		_end = wasApiData.timestamp + _duration;

		if (_start > start && again== false)
			return false;

		avalid_start = max(start, _start);
		avalid_end = min(end, _end);

		//invalid interval
		if (avalid_end <= avalid_start){

			if (_start < start){
				mMutex.lock();
				mAudioFrameQueue.pop();
				mSamples -= frames_avai;
				mMutex.unlock();
				if (wasApiData.data){
					av_freep(&wasApiData.data[0]);
					av_freep(wasApiData.data);
				}
				TRACE("drop wasapi media\n");
				continue;
			}
			else{
				break;
			}
		}
		else{
			TRACE("found start pos\n");
			start_pos = (avalid_start - start) * 44100 / 1000000 + wasApiData.start_frame;
			if (start_pos > wasApiData.nb_samples)
			{
				TRACE("start pos is too large\n");
			}
			goto FoundedStartPos;
		}

	}
	writtenSamples = nb_samples;
	return true;

FoundedStartPos:
	int samples = wasApiData.nb_samples - start_pos;
	samples = min((1024 - nb_samples), samples);
	av_samples_copy(mMixerFrame->data, wasApiData.data,
		nb_samples, start_pos, samples, wasApiData.channels, mOutSampleFmt);

	nb_samples += samples;

	int usedsamples = start_pos + samples - wasApiData.start_frame;
	wasApiData.start_frame = start_pos + samples;
	wasApiData.timestamp = avalid_end;
	if (wasApiData.start_frame >= wasApiData.nb_samples){
		mMutex.lock();
		mAudioFrameQueue.pop();
		mSamples -= frames_avai;
		mMutex.unlock();
		if (wasApiData.data){
			av_freep(&wasApiData.data[0]);
			av_freep(wasApiData.data);
		}

		if (nb_samples < 1024 && mAudioFrameQueue.size()){
			again = true;
			goto FoundStartPos;
		}
	}
	else{
		mMutex.lock();
		mAudioFrameQueue.front() = wasApiData;
		mSamples -= usedsamples;
		mMutex.unlock();
	}
	writtenSamples = nb_samples;
	return true;
}

void  wasapiSource::rawRender(AVFrame *frame)
{
	int64_t start = frame->pts;
	int64_t duration = frame->nb_samples * 1000000 / 44100;
	int64_t end = frame->pts + duration;
	int  writtenSamples = 0;
	if (selectWasapiData(writtenSamples,start, end))
	{
		if (writtenSamples){
			TRACE("wasapiSource::rawRender:%d\n", writtenSamples);
			int planar = av_sample_fmt_is_planar(mOutSampleFmt);
			int planes = planar ? frame->channels : 1;
			for (int i = 0; i < planes; i++){
				float * dst = (float *)(frame->data[i]);
				float * src = (float *)(mMixerFrame->data[i]);
				for (int j = 0; j < writtenSamples; j++){
					*(dst) += *(src);
					dst++;
					src++;
				}
			}
		}
	}
}

bool wasapiSource::getSourceFrame(AVFrame* frame, bool audio)
{
	return false;
}
AVFrame * wasapiSource::getSourceFrame(bool audio)
{
	return NULL;
}

void wasapiSource::getSourceDefaultSetting(Json::Value &setting)
{
	wasapiMonitor monitor;
	monitor.choose_wasapiDevice(setting);
}