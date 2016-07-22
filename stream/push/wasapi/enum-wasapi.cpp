#include "enum-wasapi.hpp"
#include "HRError.hpp"
#include "ComPtr.hpp""
#include "CoTaskMemPtr.hpp"
#include "../trace_debug.h"

using namespace std;

#define UNUSED_PARAMETER(param) (void)param

size_t wchar_to_utf8(const wchar_t *in, size_t insize, char *out,
	size_t outsize, int flags)
{
	int i_insize = (int)insize;
	int ret;

	if (i_insize == 0)
		i_insize = (int)wcslen(in);

	ret = WideCharToMultiByte(CP_UTF8, 0, in, i_insize, out, (int)outsize,
		NULL, NULL);

	UNUSED_PARAMETER(flags);
	return (ret > 0) ? (size_t)ret : 0;
}

size_t os_wcs_to_utf8(const wchar_t *str, size_t len, char *dst,
	size_t dst_size)
{
	size_t in_len;
	size_t out_len;

	if (!str)
		return 0;

	in_len = (len != 0) ? len : wcslen(str);
	out_len = dst ? (dst_size - 1) : wchar_to_utf8(str, in_len, NULL, 0, 0);

	if (dst) {
		if (!dst_size)
			return 0;

		if (out_len)
			out_len = wchar_to_utf8(str, in_len,
			dst, out_len + 1, 0);

		dst[out_len] = 0;
	}

	return out_len;
}


string GetDeviceName(IMMDevice *device)
{
	string device_name;
	ComPtr<IPropertyStore> store;
	HRESULT res;

	if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, store.Assign()))) {
		PROPVARIANT nameVar;

		PropVariantInit(&nameVar);
		res = store->GetValue(PKEY_Device_FriendlyName, &nameVar);

		if (SUCCEEDED(res)) {
			size_t len = wcslen(nameVar.pwszVal);
			size_t size;

			size = os_wcs_to_utf8(nameVar.pwszVal, len,
					nullptr, 0) + 1;
			device_name.resize(size);
			os_wcs_to_utf8(nameVar.pwszVal, len,
					&device_name[0], size);
		}
	}

	return device_name;
}

void GetWASAPIAudioDevices_(vector<AudioDeviceInfo> &devices, bool input)
{
	ComPtr<IMMDeviceEnumerator> enumerator;
	ComPtr<IMMDeviceCollection> collection;
	UINT count;
	HRESULT res;

	res = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
			__uuidof(IMMDeviceEnumerator),
			(void**)enumerator.Assign());
	if (FAILED(res))
		throw HRError("Failed to create enumerator", res);

	res = enumerator->EnumAudioEndpoints(input ? eCapture : eRender,
			DEVICE_STATE_ACTIVE, collection.Assign());
	if (FAILED(res))
		throw HRError("Failed to enumerate devices", res);

	res = collection->GetCount(&count);
	if (FAILED(res))
		throw HRError("Failed to get device count", res);

	for (UINT i = 0; i < count; i++) {
		ComPtr<IMMDevice>   device;
		CoTaskMemPtr<WCHAR> w_id;
		AudioDeviceInfo     info;
		size_t              len, size;

		res = collection->Item(i, device.Assign());
		if (FAILED(res))
			continue;

		res = device->GetId(&w_id);
		if (FAILED(res) || !w_id || !*w_id)
			continue;

		info.name = GetDeviceName(device);

		len  = wcslen(w_id);
		size = os_wcs_to_utf8(w_id, len, nullptr, 0) + 1;
		info.id.resize(size);
		os_wcs_to_utf8(w_id, len, &info.id[0], size);

		devices.push_back(info);
	}
}

void GetWASAPIAudioDevices(vector<AudioDeviceInfo> &devices, bool input)
{
	devices.clear();

	try {
		GetWASAPIAudioDevices_(devices, input);

	} catch (HRError error) {
		TRACE("[GetWASAPIAudioDevices] %s: %lX",
				error.str, error.hr);
	}
}
