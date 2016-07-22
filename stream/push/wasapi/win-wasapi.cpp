#include "enum-wasapi.hpp"

#include "HRError.hpp"
#include "ComPtr.hpp"
#include "WinHandle.hpp"
#include "CoTaskMemPtr.hpp"
#include "../config.h"
#include "../wasapiSource.h"

using namespace std;

static inline bool has_utf8_bom(const char *in_char)
{
	uint8_t *in = (uint8_t*)in_char;
	return (in && in[0] == 0xef && in[1] == 0xbb && in[2] == 0xbf);
}

size_t utf8_to_wchar(const char *in, size_t insize, wchar_t *out,
	size_t outsize, int flags)
{
	int i_insize = (int)insize;
	int ret;

	if (i_insize == 0)
		i_insize = (int)strlen(in);

	/* prevent bom from being used in the string */
	if (has_utf8_bom(in)) {
		if (i_insize >= 3) {
			in += 3;
			insize -= 3;
		}
	}

	ret = MultiByteToWideChar(CP_UTF8, 0, in, i_insize, out, (int)outsize);

	return (ret > 0) ? (size_t)ret : 0;
}


size_t os_utf8_to_wcs(const char *str, size_t len, wchar_t *dst,
	size_t dst_size)
{
	size_t in_len;
	size_t out_len;

	if (!str)
		return 0;

	in_len = len ? len : strlen(str);
	out_len = dst ? (dst_size - 1) : utf8_to_wchar(str, in_len, NULL, 0, 0);

	if (dst) {
		if (!dst_size)
			return 0;

		if (out_len)
			out_len = utf8_to_wchar(str, in_len,
			dst, out_len + 1, 0);

		dst[out_len] = 0;
	}

	return out_len;
}


size_t os_utf8_to_wcs_ptr(const char *str, size_t len, wchar_t **pstr)
{
	if (str) {
		size_t out_len = os_utf8_to_wcs(str, len, NULL, 0);

		*pstr = (wchar_t *)malloc((out_len + 1) * sizeof(wchar_t));
		return os_utf8_to_wcs(str, len, *pstr, out_len + 1);
	}
	else {
		*pstr = NULL;
		return 0;
	}
}




WASAPISource::WASAPISource(Json::Value &settings, wasapi_callback* callback, bool input)
	: isInputDevice(input),
	mCallback(callback)
{
	UpdateSettings(settings);

	stopSignal = CreateEvent(nullptr, true, false, nullptr);
	if (!stopSignal.Valid())
		throw "Could not create stop signal";

	receiveSignal = CreateEvent(nullptr, false, false, nullptr);
	if (!receiveSignal.Valid())
		throw "Could not create receive signal";

	Start();
}

inline void WASAPISource::Start()
{
	if (!TryInitialize()) {
		TRACE("[WASAPISource::WASAPISource] "
		               "Device '%s' not found.  Waiting for device",
		               device_id.c_str());
		Reconnect();
	}
}

inline void WASAPISource::Stop()
{
	SetEvent(stopSignal);

	if (active) {
		TRACE("WASAPI: Device '%s' Terminated",
				device_name.c_str());
		WaitForSingleObject(captureThread, INFINITE);
	}

	if (reconnecting)
		WaitForSingleObject(reconnectThread, INFINITE);

	ResetEvent(stopSignal);
}

inline WASAPISource::~WASAPISource()
{
	Stop();
}

void WASAPISource::UpdateSettings(Json::Value &settings)
{
	device_id = settings[OPT_DEVICE_ID].asString();
	useDeviceTiming = settings[OPT_USE_DEVICE_TIMING].asBool();
	isDefaultDevice = _strcmpi(device_id.c_str(), "default") == 0;
}

void WASAPISource::Update(Json::Value &settings)
{
	string newDevice = settings[OPT_DEVICE_ID].asString();
	bool restart = newDevice.compare(device_id) != 0;

	if (restart)
		Stop();

	UpdateSettings(settings);

	if (restart)
		Start();
}

bool WASAPISource::InitDevice(IMMDeviceEnumerator *enumerator)
{
	HRESULT res;

	if (isDefaultDevice) {
		res = enumerator->GetDefaultAudioEndpoint(
				isInputDevice ? eCapture        : eRender,
				isInputDevice ? eCommunications : eConsole,
				device.Assign());
	} else {
		wchar_t *w_id;
		os_utf8_to_wcs_ptr(device_id.c_str(), device_id.size(), &w_id);

		res = enumerator->GetDevice(w_id, device.Assign());

		free(w_id);
	}

	return SUCCEEDED(res);
}

#define BUFFER_TIME_100NS (5*10000000)

void WASAPISource::InitClient()
{
	CoTaskMemPtr<WAVEFORMATEX> wfex;
	HRESULT                    res;
	DWORD                      flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;

	res = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
			nullptr, (void**)client.Assign());
	if (FAILED(res))
		throw HRError("Failed to activate client context", res);

	res = client->GetMixFormat(&wfex);
	if (FAILED(res))
		throw HRError("Failed to get mix format", res);

	InitFormat(wfex);

	if (!isInputDevice)
		flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;

	res = client->Initialize(
			AUDCLNT_SHAREMODE_SHARED, flags,
			BUFFER_TIME_100NS, 0, wfex, nullptr);
	if (FAILED(res))
		throw HRError("Failed to get initialize audio client", res);
}

void WASAPISource::InitRender()
{
	CoTaskMemPtr<WAVEFORMATEX> wfex;
	HRESULT                    res;
	LPBYTE                     buffer;
	UINT32                     frames;
	ComPtr<IAudioClient>       client;

	res = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
			nullptr, (void**)client.Assign());
	if (FAILED(res))
		throw HRError("Failed to activate client context", res);

	res = client->GetMixFormat(&wfex);
	if (FAILED(res))
		throw HRError("Failed to get mix format", res);

	res = client->Initialize(
			AUDCLNT_SHAREMODE_SHARED, 0,
			BUFFER_TIME_100NS, 0, wfex, nullptr);
	if (FAILED(res))
		throw HRError("Failed to get initialize audio client", res);

	/* Silent loopback fix. Prevents audio stream from stopping and */
	/* messing up timestamps and other weird glitches during silence */
	/* by playing a silent sample all over again. */

	res = client->GetBufferSize(&frames);
	if (FAILED(res))
		throw HRError("Failed to get buffer size", res);

	res = client->GetService(__uuidof(IAudioRenderClient),
		(void**)render.Assign());
	if (FAILED(res))
		throw HRError("Failed to get render client", res);

	res = render->GetBuffer(frames, &buffer);
	if (FAILED(res))
		throw HRError("Failed to get buffer", res);

	memset(buffer, 0, frames*wfex->nBlockAlign);

	render->ReleaseBuffer(frames, 0);
}

static speaker_layout ConvertSpeakerLayout(DWORD layout, WORD channels)
{
	switch (layout) {
	case KSAUDIO_SPEAKER_QUAD:             return SPEAKERS_QUAD;
	case KSAUDIO_SPEAKER_2POINT1:          return SPEAKERS_2POINT1;
	case KSAUDIO_SPEAKER_4POINT1:          return SPEAKERS_4POINT1;
	case KSAUDIO_SPEAKER_SURROUND:         return SPEAKERS_SURROUND;
	case KSAUDIO_SPEAKER_5POINT1:          return SPEAKERS_5POINT1;
	case KSAUDIO_SPEAKER_5POINT1_SURROUND: return SPEAKERS_5POINT1_SURROUND;
	case KSAUDIO_SPEAKER_7POINT1:          return SPEAKERS_7POINT1;
	case KSAUDIO_SPEAKER_7POINT1_SURROUND: return SPEAKERS_7POINT1_SURROUND;
	}

	return (speaker_layout)channels;
}

void WASAPISource::InitFormat(WAVEFORMATEX *wfex)
{
	DWORD layout = 0;

	if (wfex->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		WAVEFORMATEXTENSIBLE *ext = (WAVEFORMATEXTENSIBLE*)wfex;
		layout = ext->dwChannelMask;
	}

	/* WASAPI is always float */
	sampleRate = wfex->nSamplesPerSec;
	format = AV_SAMPLE_FMT_FLT;
	speakers   = ConvertSpeakerLayout(layout, wfex->nChannels);
}

void WASAPISource::InitCapture()
{
	HRESULT res = client->GetService(__uuidof(IAudioCaptureClient),
			(void**)capture.Assign());
	if (FAILED(res))
		throw HRError("Failed to create capture context", res);

	res = client->SetEventHandle(receiveSignal);
	if (FAILED(res))
		throw HRError("Failed to set event handle", res);

	captureThread = CreateThread(nullptr, 0,
			WASAPISource::CaptureThread, this,
			0, nullptr);
	if (!captureThread.Valid())
		throw "Failed to create capture thread";

	client->Start();
	active = true;

	TRACE("WASAPI: Device '%s' initialized", device_name.c_str());
}

void WASAPISource::Initialize()
{
	ComPtr<IMMDeviceEnumerator> enumerator;
	HRESULT res;

	res = CoCreateInstance(__uuidof(MMDeviceEnumerator),
			nullptr, CLSCTX_ALL,
			__uuidof(IMMDeviceEnumerator),
			(void**)enumerator.Assign());
	if (FAILED(res))
		throw HRError("Failed to create enumerator", res);

	if (!InitDevice(enumerator))
		return;

	device_name = GetDeviceName(device);

	InitClient();
	if (!isInputDevice) InitRender();
	InitCapture();
}

bool WASAPISource::TryInitialize()
{
	try {
		Initialize();

	} catch (HRError error) {
		if (previouslyFailed)
			return active;

		TRACE("[WASAPISource::TryInitialize]:[%s] %s: %lX",
				device_name.empty() ?
					device_id.c_str() : device_name.c_str(),
				error.str, error.hr);

	} catch (const char *error) {
		if (previouslyFailed)
			return active;

		TRACE("[WASAPISource::TryInitialize]:[%s] %s",
				device_name.empty() ?
					device_id.c_str() : device_name.c_str(),
				error);
	}

	previouslyFailed = !active;
	return active;
}

void WASAPISource::Reconnect()
{
	reconnecting = true;
	reconnectThread = CreateThread(nullptr, 0,
			WASAPISource::ReconnectThread, this,
			0, nullptr);

	if (!reconnectThread.Valid())
		TRACE("[WASAPISource::Reconnect] "
		                "Failed to intiialize reconnect thread: %lu",
		                 GetLastError());
}

static inline bool WaitForSignal(HANDLE handle, DWORD time)
{
	return WaitForSingleObject(handle, time) != WAIT_TIMEOUT;
}

#define RECONNECT_INTERVAL 3000


#define VC_EXCEPTION 0x406D1388

#pragma pack(push,8)
struct vs_threadname_info {
	DWORD type; /* 0x1000 */
	const char *name;
	DWORD thread_id;
	DWORD flags;
};
#pragma pack(pop)


#define THREADNAME_INFO_SIZE \
        (sizeof(struct vs_threadname_info) / sizeof(ULONG_PTR))

void os_set_thread_name(const char *name)
{
#ifdef __MINGW32__
	UNUSED_PARAMETER(name);
#else                       
	struct vs_threadname_info info;
	info.type = 0x1000;
	info.name = name;
	info.thread_id = GetCurrentThreadId();
	info.flags = 0;

#ifdef NO_SEH_MINGW
	__try1(EXCEPTION_EXECUTE_HANDLER) {
#else
	__try {
#endif
		RaiseException(VC_EXCEPTION, 0, THREADNAME_INFO_SIZE,
			(ULONG_PTR*)&info);
#ifdef NO_SEH_MINGW
	} __except1{
#else
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
#endif
	}
#endif
}

DWORD WINAPI WASAPISource::ReconnectThread(LPVOID param)
{
	WASAPISource *source = (WASAPISource*)param;

	os_set_thread_name("win-wasapi: reconnect thread");

	while (!WaitForSignal(source->stopSignal, RECONNECT_INTERVAL)) {
		if (source->TryInitialize())
			break;
	}

	source->reconnectThread = nullptr;
	source->reconnecting = false;
	return 0;
}

bool WASAPISource::ProcessCaptureData()
{
	HRESULT res;
	LPBYTE  buffer;
	UINT32  frames;
	DWORD   flags;
	UINT64  pos, ts;
	UINT    captureSize = 0;

	while (true) {
		res = capture->GetNextPacketSize(&captureSize);

		if (FAILED(res)) {
			if (res != AUDCLNT_E_DEVICE_INVALIDATED)
				TRACE("[WASAPISource::GetCaptureData]"
						" capture->GetNextPacketSize"
						" failed: %lX", res);
			return false;
		}

		if (!captureSize)
			break;

		res = capture->GetBuffer(&buffer, &frames, &flags, &pos, &ts);
		if (FAILED(res)) {
			if (res != AUDCLNT_E_DEVICE_INVALIDATED)
				TRACE("[WASAPISource::GetCaptureData]"
						" capture->GetBuffer"
						" failed: %lX", res);
			return false;
		}

		wasapiSourceData data = {};
		data.data          = ( uint8_t*)buffer;
		data.frames           = (uint32_t)frames;
		data.speakers         = speakers;
		data.samples_per_sec  = sampleRate;
		data.format           = format;
		data.timestamp        = useDeviceTiming ?
			ts/10 : av_gettime_relative();

		mCallback->raw_audio(data);

		capture->ReleaseBuffer(frames);
	}

	return true;
}

static inline bool WaitForCaptureSignal(DWORD numSignals, const HANDLE *signals,
		DWORD duration)
{
	DWORD ret;
	ret = WaitForMultipleObjects(numSignals, signals, false, duration);

	return ret == WAIT_OBJECT_0 || ret == WAIT_TIMEOUT;
}

DWORD WINAPI WASAPISource::CaptureThread(LPVOID param)
{
	WASAPISource *source   = (WASAPISource*)param;
	bool         reconnect = false;

	/* Output devices don't signal, so just make it check every 10 ms */
	DWORD        dur       = source->isInputDevice ? INFINITE : 10;

	HANDLE sigs[2] = {
		source->receiveSignal,
		source->stopSignal
	};

	os_set_thread_name("win-wasapi: capture thread");

	while (WaitForCaptureSignal(2, sigs, dur)) {
		if (!source->ProcessCaptureData()) {
			reconnect = true;
			break;
		}
	}

	source->client->Stop();

	source->captureThread = nullptr;
	source->active        = false;

	if (reconnect) {
		TRACE("Device '%s' invalidated.  Retrying",
				source->device_name.c_str());
		source->Reconnect();
	}

	return 0;
}


