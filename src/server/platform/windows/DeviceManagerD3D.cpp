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

	UINT flag = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#if !defined(NDEBUG) && defined(DAYLIGHT_D3D_DEBUG)
	flag |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1 };
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