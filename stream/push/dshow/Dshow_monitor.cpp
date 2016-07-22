#include "Dshow_monitor.h"

extern "C"{
	extern char *dup_wchar_to_utf8(wchar_t *w);
	extern enum AVPixelFormat dshow_pixfmt(DWORD biCompression, WORD biBitCount);
}

//device capability
struct VideoStreamCAPS
{
	enum AVPixelFormat pix_fmt;
	enum AVCodecID codec_id;
	int width;
	int height;
	int minFps;
	int maxFps;
};

struct AudioStreamCAPS
{
	int minChannels;
	int maxChannels;
	int minBitsPerSample;
	int maxBitsPerSample;
	int minSampleFrequency;
	int maxSampleFrequency;
};

//input format
struct VideoDshowFormat
{
	enum AVPixelFormat pix_fmt;
	int width;
	int height;
	int Fps;
	char *videoName;
};

struct AudioDshowFormat
{
	int channel;
	int bitsPerSample;
	int frequency;
	char *audioName;
};

struct Resolution{
	int width;
	int height;
};

Dshow_monitor::Dshow_monitor()
{
	CoInitialize(0);
	clear();
	flushDeviceList(false);
	flushDeviceList(true);
}


Dshow_monitor::~Dshow_monitor()
{
	CoUninitialize();
}

void Dshow_monitor::clear()
{
	//std::map<unique_name_ptr, frendly_name_ptr>::iterator it;
	for (auto it = mVideoDevice.begin(); it != mVideoDevice.end(); it++)
	{
		av_free(it->first);
		av_free(it->second);
	}

	mVideoDevice.clear();

	for (auto it = mAudioDevice.begin(); it != mAudioDevice.end(); it++)
	{
		av_free(it->first);
		av_free(it->second);
	}

	mAudioDevice.clear();

	mVideoCaps.clear();
	mAudioCaps.clear();
}

int Dshow_monitor::flushDeviceList(bool audio)
{
	ICreateDevEnum *devenum = NULL;
	IEnumMoniker *classenum = NULL;
	IMoniker *m = NULL;
	const GUID device_guid[2] = { CLSID_VideoInputDeviceCategory,
		                          CLSID_AudioInputDeviceCategory };

	int r = S_OK;
	r = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
		                 IID_ICreateDevEnum, (void **)&devenum);
	if (r != S_OK) {
		TRACE("%s:Could not enumerate %s devices.\n", __FUNCTION__, audio ? "audio":"video");
		goto error;
	}

	r = devenum->CreateClassEnumerator(device_guid[audio],
		                               (IEnumMoniker **)&classenum, 0);
	if (r != S_OK) {
		TRACE("%s:Could not enumerate %s devices.\n", __FUNCTION__, audio ? "audio" : "video");
		goto error;
	}

	while (classenum->Next(1, &m, NULL) == S_OK){
		IPropertyBag *bag = NULL;
		char *friendly_name = NULL;
		char *unique_name = NULL;
		VARIANT var;
		IBindCtx *bind_ctx = NULL;
		LPOLESTR olestr = NULL;
		LPMALLOC co_malloc = NULL;

		r = CoGetMalloc(1, &co_malloc);
		if (r != S_OK)
			goto fail1;
		r = CreateBindCtx(0, &bind_ctx);
		if (r != S_OK)
			goto fail1;

		/* GetDisplayname works for both video and audio, DevicePath doesn't */
		r = m->GetDisplayName(bind_ctx, NULL, &olestr);
		if (r != S_OK)
			goto fail1;

		unique_name = dup_wchar_to_utf8(olestr);
		/* replace ':' with '_' since we use : to delineate between sources */
		for (int i = 0; i < strlen(unique_name); i++) {
			if (unique_name[i] == ':')
				unique_name[i] = '_';
		}

		r = m->BindToStorage(NULL, NULL, IID_IPropertyBag, (void **)&bag);
		if (r != S_OK)
			goto fail1;

		var.vt = VT_BSTR;
		r = bag->Read(L"FriendlyName", &var, NULL);
		if (r != S_OK)
			goto fail1;

		friendly_name = dup_wchar_to_utf8(var.bstrVal);
	fail1:	
		if (unique_name && friendly_name){
			if (audio){
				mAudioDevice.insert(std::pair<unique_name_ptr, frendly_name_ptr>(unique_name, friendly_name));
			}
			else{
				mVideoDevice.insert(std::pair<unique_name_ptr, frendly_name_ptr>(unique_name, friendly_name));
			}
			IBaseFilter *device_filter = NULL;
			if (S_OK == m->BindToObject(0, 0, IID_IBaseFilter, (void **)&device_filter)){
				getDeviceCaps(unique_name, device_filter, audio);
				device_filter->Release();
			}
		}
		else{
			av_freep(&unique_name);
			av_freep(&friendly_name);
		}
		if (olestr && co_malloc)
			co_malloc->Free(olestr);

		if (bind_ctx)
			bind_ctx->Release();



		if (bag)
			bag->Release();
		m->Release();
	}
error:
	if (classenum)
		classenum->Release();

	if (devenum)
		devenum->Release();

	if (r != S_OK){
		return -1;
	}
	else{
		return 0;
	}
}

int Dshow_monitor::getDeviceCaps(unique_name_ptr id, IBaseFilter *device_filter,bool audio)
{
	IEnumPins *pins = NULL;
	IPin *pin =NULL;
	int r;

	const GUID mediatype[2] = { MEDIATYPE_Video, MEDIATYPE_Audio };

	r = device_filter->EnumPins(&pins);
	if (r != S_OK) {
		TRACE("%s:Could not enumerate pins.\n",__FUNCTION__);
		goto error;
	}

	while (pins->Next(1, &pin, NULL) == S_OK) {
		IKsPropertySet *p = NULL;
		PIN_INFO info = { 0 };
		GUID category;
		DWORD r2;

		pin->QueryPinInfo(&info);
		info.pFilter->Release();

		if (info.dir != PINDIR_OUTPUT)
			goto next;
		if (pin->QueryInterface(IID_IKsPropertySet, (void **)&p) != S_OK)
			goto next;
		if (p->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY,
			NULL, 0, &category, sizeof(GUID), &r2) != S_OK)
			goto next;
		if (!IsEqualGUID(category, PIN_CATEGORY_CAPTURE))
			goto next;

		dshow_cycle_formats(pin, id, audio);

	next:
		if (p)
			p->Release();
		if (pin)
			pin->Release();
	}
	if (pins)
		pins->Release();

error:
	return 0;
}

void Dshow_monitor::dshow_cycle_formats(IPin *pin, unique_name_ptr id,bool audio)
{
	IAMStreamConfig *config = NULL;
	AM_MEDIA_TYPE *type = NULL;
	void *caps = NULL;
	int n, size, r=S_OK;

	if (pin->QueryInterface(IID_IAMStreamConfig, (void **)&config) != S_OK)
		return;

	if (config->GetNumberOfCapabilities(&n, &size) != S_OK)
		goto end;

	caps = av_malloc(size);
	if (!caps)
		goto end;

	for (int i = 0; i < n ; i++) {
		r = config->GetStreamCaps( i, &type, (BYTE *)caps);
		if (r != S_OK)
			goto next;

		if (!audio) {
			VIDEO_STREAM_CONFIG_CAPS *vcaps = (VIDEO_STREAM_CONFIG_CAPS *)caps;
			BITMAPINFOHEADER *bih;
			int64_t *fr;
			const AVCodecTag *const tags[] = { avformat_get_riff_video_tags(), NULL };
			if (IsEqualGUID(type->formattype, FORMAT_VideoInfo)) {
				VIDEOINFOHEADER *v = (VIDEOINFOHEADER *)type->pbFormat;
				fr = &v->AvgTimePerFrame;
				bih = &v->bmiHeader;
			}
			else if (IsEqualGUID(type->formattype, FORMAT_VideoInfo2)) {
				VIDEOINFOHEADER2 *v = (VIDEOINFOHEADER2 *)type->pbFormat;
				fr = &v->AvgTimePerFrame;
				bih = &v->bmiHeader;
			}
			else {
				goto next;
			}

			VideoStreamCAPS videoCap;
			memset(&videoCap, 0, sizeof(struct VideoStreamCAPS));

			enum AVPixelFormat pix_fmt = dshow_pixfmt(bih->biCompression, bih->biBitCount);
			enum AVCodecID codec_id = AV_CODEC_ID_NONE;
			if (pix_fmt == AV_PIX_FMT_NONE) {
				codec_id = av_codec_get_id(tags, bih->biCompression);
			}

			videoCap.pix_fmt = pix_fmt;
			videoCap.codec_id = codec_id;
			videoCap.width = vcaps->MinOutputSize.cx;
			videoCap.height =vcaps->MinOutputSize.cy;
			videoCap.minFps = (int)(1e7 / vcaps->MaxFrameInterval);
			videoCap.maxFps = (int)(1e7 / vcaps->MinFrameInterval);

			for (auto it = mVideoCaps[id].begin(); it != mVideoCaps[id].end(); ++it){
				VideoStreamCAPS temp=*it;
				if (0 == memcmp(&temp, &videoCap, sizeof(struct VideoStreamCAPS))){
					goto next;
				}
			}
			mVideoCaps[id].push_back(videoCap);
			goto next;
		}
		else {
			AUDIO_STREAM_CONFIG_CAPS *acaps = (AUDIO_STREAM_CONFIG_CAPS*)caps;
			WAVEFORMATEX *fx;
			struct AudioStreamCAPS audioCaps;
			memset(&audioCaps, 0, sizeof(struct AudioStreamCAPS));
			if (IsEqualGUID(type->formattype, FORMAT_WaveFormatEx)) {
				fx = (WAVEFORMATEX *)type->pbFormat;
			}
			else {
				goto next;
			}

			audioCaps.minChannels = acaps->MinimumChannels;
			audioCaps.maxChannels = acaps->MaximumChannels;
			audioCaps.minSampleFrequency = acaps->MinimumSampleFrequency;
			audioCaps.maxSampleFrequency = acaps->MaximumSampleFrequency;
			audioCaps.minBitsPerSample = acaps->MinimumBitsPerSample;
			audioCaps.maxBitsPerSample = acaps->MaximumBitsPerSample;
			{
				struct AudioStreamCAPS temp = mAudioCaps[id];
				if (0 == memcmp(&temp, &audioCaps, sizeof(struct AudioStreamCAPS)))
					goto next;
				mAudioCaps[id] = audioCaps;
			}
			goto next;
		}
	next:
		if (type->pbFormat)
			CoTaskMemFree(type->pbFormat);
		CoTaskMemFree(type);
	}
end:
	if (config)
		config->Release();
	av_free(caps);
}

//select device
void Dshow_monitor::chooseAudioDeviceAndConfig(Json::Value & audioSettings)
{
	AudioDshowFormat dshowFormat;
	std::vector<unique_name_ptr> AudioDeviceArray;
	int input = 0;
	unique_name_ptr unique_id = NULL;
	memset(&dshowFormat, 0, sizeof(AudioDshowFormat));
	dshowFormat.bitsPerSample = 16;

	for (auto it = mAudioDevice.begin(); it != mAudioDevice.end(); it++)
	{
		AudioDeviceArray.push_back(it->first);
	}
	
DeviceSelect:
	printf("please select audio device:\n");
	for (int i = 0; i < AudioDeviceArray.size(); i++){
		printf("  %d:%s\n", i, mAudioDevice[AudioDeviceArray[i]]);
	}
	scanf("%d", &input);
	
	if (input < 0 || input >= AudioDeviceArray.size()){
		printf("invalid audio device,please reselect:\n");
		goto DeviceSelect;
	}
	unique_id = AudioDeviceArray[input];
	dshowFormat.audioName = av_strdup(mAudioDevice[unique_id]);
ChannelSelect:
	printf("please select audio channel:%d~%d\n", 
		mAudioCaps[unique_id].minChannels, mAudioCaps[unique_id].maxChannels);
	scanf("%d", &input);
	if (input<mAudioCaps[unique_id].minChannels || input>mAudioCaps[unique_id].maxChannels){
		printf("invalid audio channel,please reselect:\n");
		goto ChannelSelect;
	}
	dshowFormat.channel = input;

FrequencySelect:
	printf("please select audio frequency:%d to %d\n",
		mAudioCaps[unique_id].minSampleFrequency, mAudioCaps[unique_id].maxSampleFrequency);
	scanf("%d", &input);
	if (input<mAudioCaps[unique_id].minSampleFrequency || input>mAudioCaps[unique_id].maxSampleFrequency){
		printf("invalid audio frequency,please reselect:\n");
		goto FrequencySelect;
	}
	dshowFormat.frequency = input;

	audioSettings["audioDeviceName"] = dshowFormat.audioName;
	audioSettings["audioSampleRate"] = dshowFormat.frequency;
	audioSettings["audioBitsPerSample"] = dshowFormat.bitsPerSample;
	audioSettings["audioChannel"] = dshowFormat.channel;
	av_free(dshowFormat.audioName);
}


void Dshow_monitor::chooseVideoDeviceAndConfig(Json::Value & videoSettings)
{
	VideoDshowFormat dshowFormat;
	int input = 0;
	unique_name_ptr unique_id = NULL;
	memset(&dshowFormat, 0, sizeof(VideoDshowFormat));
//
	std::vector<unique_name_ptr> VideoDeviceArray;
	for (auto it = mVideoDevice.begin(); it != mVideoDevice.end(); it++)
	{
		VideoDeviceArray.push_back(it->first);
	}
DeviceSelect:
	printf("please select video device:\n");
	for (int i = 0; i < VideoDeviceArray.size(); i++){
		printf("  %d:%s\n", i, mVideoDevice[VideoDeviceArray[i]]);
	}
	scanf("%d", &input);

	if (input < 0 || input >= VideoDeviceArray.size()){
		printf("invalid video device,please reselect:\n");
		goto DeviceSelect;
	}
	unique_id = VideoDeviceArray[input];
	dshowFormat.videoName = av_strdup(mVideoDevice[unique_id]);

//
	std::vector<Resolution> VideoResolutionsArray;
	for (auto it = mVideoCaps[unique_id].begin(); it != mVideoCaps[unique_id].end(); ++it){
		VideoStreamCAPS temp = *it;
		Resolution size;
		size.width = temp.width;
		size.height = temp.height;
		VideoResolutionsArray.push_back(size);
	}
ResolutionSelect:
	printf("please select video resolution:\n");
	for (int i = 0; i < VideoResolutionsArray.size(); i++){
		printf("  %d:%dx%d\n", i, VideoResolutionsArray[i].width, VideoResolutionsArray[i].height);
	}
	scanf("%d", &input);
	if (input < 0 || input >= VideoResolutionsArray.size()){
		printf("invalid video resolution,please reselect:\n");
		goto ResolutionSelect;
	}
	dshowFormat.width = VideoResolutionsArray[input].width;
	dshowFormat.height = VideoResolutionsArray[input].height;

//
	std::vector<AVPixelFormat> videoPixelArray;
	for (auto it = mVideoCaps[unique_id].begin(); it != mVideoCaps[unique_id].end(); ++it){
		VideoStreamCAPS temp = *it;
		if (temp.width == dshowFormat.width &&
			temp.height == dshowFormat.height&&
			temp.pix_fmt != AV_PIX_FMT_NONE){
			videoPixelArray.push_back(temp.pix_fmt);
		}
	}
PixerFormatSelect:
	printf("please select video PixerFormat:\n");
	for (int i = 0; i < videoPixelArray.size(); i++){
		printf("  %d:%s\n", i, av_get_pix_fmt_name(videoPixelArray[i]));
	}
	scanf("%d", &input);
	if (input < 0 || input >= videoPixelArray.size()){
		printf("invalid video PixerFormat,please reselect:\n");
		goto PixerFormatSelect;
	}
	dshowFormat.pix_fmt = videoPixelArray[input];

//
	int minFps = 0;
	int maxFps = 0;
	for (auto it = mVideoCaps[unique_id].begin(); it != mVideoCaps[unique_id].end(); ++it){
		VideoStreamCAPS temp = *it;
		if (temp.width == dshowFormat.width &&
			temp.height == dshowFormat.height&&
			temp.pix_fmt == dshowFormat.pix_fmt){
			minFps = temp.minFps;
			maxFps = temp.maxFps;
			break;
		}
	}
FPSSelect:
	printf("please select video FPS:%d to %d\n",minFps,maxFps);
	scanf("%d", &input);
	if (input < minFps || input >maxFps){
		printf("invalid video FPS,please reselect:\n");
		goto FPSSelect;
	}
	dshowFormat.Fps = input;

	videoSettings["videoPixFmt"] = dshowFormat.pix_fmt;
	videoSettings["videoDeviceName"] = dshowFormat.videoName;
	videoSettings["videoWidth"] = dshowFormat.width;
	videoSettings["videoHeight"] = dshowFormat.height;
	videoSettings["videoFps"] = dshowFormat.Fps;

	av_free(dshowFormat.videoName);
}