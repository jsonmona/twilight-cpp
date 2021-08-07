#include "DeviceManagerD3D.h"


static void getMainAdapterWithOutput(const DxgiFactory5& factory, DxgiAdapter1* outAdapter, DxgiOutput5* outOutput) {
	HRESULT hr;

	for (UINT i = 0; i < UINT_MAX; i++) {
		outAdapter->release();
		hr = factory->EnumAdapters1(i, outAdapter->data());
		if (FAILED(hr))
			break;

		for (UINT j = 0; j < UINT_MAX; j++) {
			DxgiOutput output;
			hr = (*outAdapter)->EnumOutputs(j, output.data());
			if (FAILED(hr))
				break;

			*outOutput = output.castTo<IDXGIOutput5>();
			if (outOutput->isValid())
				return;
		}
	}

	outAdapter->release();
	outOutput->release();
}

static std::string intoUTF8(const std::wstring_view& wideStr) {
	static_assert(sizeof(wchar_t) == sizeof(WCHAR), "Expects wchar_t == WCHAR (from winnt)");

	std::string ret;
	ret.resize(wideStr.size() * 4);
	int usedSize = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideStr.data(), wideStr.size(),
			ret.data(), ret.size(), nullptr, nullptr);

	if (usedSize >= 0) {
		ret.resize(usedSize);
		return ret;
	}
	else if (usedSize == ERROR_INSUFFICIENT_BUFFER) {
		int targetSize = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideStr.data(), wideStr.size(),
				nullptr, -1, nullptr, nullptr);
		ret.resize(targetSize);
		usedSize = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideStr.data(), wideStr.size(),
			ret.data(), ret.size(), nullptr, nullptr);
		ret.resize(usedSize);
		return ret;
	}
	else {
		//FIXME: Add a proper error logging mechanism
		ret.resize(0);
		ret.append("<Failed to convert wide string into UTF-8>");
		return ret;
	}
}

DeviceManagerD3D::DeviceManagerD3D() : log(createNamedLogger("DeviceManagerD3D")) {
	HRESULT hr;

	UINT flags = 0;
#if !defined(NDEBUG) && defined(DAYLIGHT_D3D_DEBUG)
	flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
	hr = CreateDXGIFactory2(flags, dxgiFactory.guid(), (void**)dxgiFactory.data());
	check_quit(FAILED(hr), log, "Failed to create DXGI factory 5");

	getMainAdapterWithOutput(dxgiFactory, &adapter, &output);
	check_quit(adapter.isInvalid(), log, "Failed to find main adapter and output");

	DXGI_ADAPTER_DESC1 adapterDesc;
	adapter->GetDesc1(&adapterDesc);

	DXGI_OUTPUT_DESC outputDesc;
	output->GetDesc(&outputDesc);

	log->info("Selected DXGI adapter: {}", intoUTF8(adapterDesc.Description));
	log->info("Selected DXGI output: {}", intoUTF8(outputDesc.DeviceName));

	UINT flag = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#if !defined(NDEBUG) && defined(DAYLIGHT_D3D_DEBUG)
	flag |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_10_0 };
	hr = D3D11CreateDevice(adapter.ptr(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flag,
		featureLevels, 1, D3D11_SDK_VERSION, device.data(), nullptr, context.data());
	if (FAILED(hr)) {
		flag &= ~D3D11_CREATE_DEVICE_VIDEO_SUPPORT;

		hr = D3D11CreateDevice(adapter.ptr(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flag,
			featureLevels, 1, D3D11_SDK_VERSION, device.data(), nullptr, context.data());
		check_quit(FAILED(hr), log, "Unable to create D3D11 device ({:#x})", hr);
	}
	else {
		hasVideoSupport = true;

		hr = MFCreateDXGIDeviceManager(&mfDeviceManagerResetToken, mfDeviceManager.data());
		check_quit(FAILED(hr), log, "Failed to create Media Foundation DXGI device manager");

		hr = mfDeviceManager->ResetDevice(device.ptr(), mfDeviceManagerResetToken);
		check_quit(FAILED(hr), log, "Failed to reset device");
	}

	device.castTo<ID3D10Multithread>()->SetMultithreadProtected(true);
}

bool DeviceManagerD3D::isVideoSupported() {
	return hasVideoSupport;
}