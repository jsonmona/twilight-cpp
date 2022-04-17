#include "DxgiHelper.h"

TWILIGHT_DEFINE_LOGGER(DxgiHelper);

static std::string intoUTF8(const std::wstring_view &wideStr) {
    static_assert(sizeof(wchar_t) == sizeof(WCHAR), "Expects wchar_t == WCHAR (from winnt)");

    std::string ret;
    ret.resize(wideStr.size() * 4);
    int usedSize = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideStr.data(), wideStr.size(), ret.data(),
                                       ret.size(), nullptr, nullptr);

    if (usedSize >= 0) {
        ret.resize(usedSize);
        return ret;
    } else if (usedSize == ERROR_INSUFFICIENT_BUFFER) {
        int targetSize = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideStr.data(), wideStr.size(), nullptr, -1,
                                             nullptr, nullptr);
        ret.resize(targetSize);
        usedSize = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideStr.data(), wideStr.size(), ret.data(),
                                       ret.size(), nullptr, nullptr);
        ret.resize(usedSize);
        return ret;
    } else {
        // FIXME: Add a proper error logging mechanism
        ret.resize(0);
        ret.append("<Failed to convert wide string into UTF-8>");
        return ret;
    }
}

static std::string intoString(const GUID &guid) {
    OLECHAR buf[64];
    int len = StringFromGUID2(guid, buf, 64);
    if (len <= 0)
        return "<error>";
    return intoUTF8(std::wstring_view(buf, len - 1));
}

DxgiHelper::DxgiHelper() {
    HRESULT hr;

    UINT flags = 0;
#if !defined(NDEBUG) && defined(TWILIGHT_D3D_DEBUG)
    flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

    hr = CreateDXGIFactory2(flags, factory.guid(), (void **)factory.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create DXGI factory 5");
}

DxgiHelper::~DxgiHelper() {}

std::vector<DxgiOutput5> DxgiHelper::findAllOutput() {
    HRESULT hr;
    std::vector<DxgiOutput5> ret;

    for (UINT i = 0; i < UINT_MAX; i++) {
        DxgiAdapter1 adapter;
        hr = factory->EnumAdapters1(i, adapter.data());
        if (FAILED(hr))
            break;

        for (UINT j = 0; j < UINT_MAX; j++) {
            DxgiOutput output;
            hr = adapter->EnumOutputs(j, output.data());
            if (FAILED(hr))
                break;

            DxgiOutput5 output5 = output.castTo<IDXGIOutput5>();
            if (output5.isValid())
                ret.push_back(std::move(output5));
        }
    }

    return ret;
}

D3D11Device DxgiHelper::createDevice(IDXGIAdapter *adapter, bool requireVideo) {
    HRESULT hr;
    D3D11Device device;

    bool releaseAdapter = false;
    if (adapter == nullptr) {
        releaseAdapter = true;
        hr = factory->EnumAdapters(0, &adapter);
        if (FAILED(hr))
            return device;
    }

    /*
    DXGI_ADAPTER_DESC1 adapterDesc;
    adapter->GetDesc1(&adapterDesc);

    DXGI_OUTPUT_DESC outputDesc;
    output->GetDesc(&outputDesc);

    DISPLAY_DEVICE dispDev = {};
    dispDev.cb = sizeof(dispDev);
    EnumDisplayDevices(outputDesc.DeviceName, 0, &dispDev, 0);

    log->info("Selected DXGI adapter: {}", intoUTF8(adapterDesc.Description));
    log->info("Selected DXGI output: {} ({})", intoUTF8(dispDev.DeviceString), intoUTF8(outputDesc.DeviceName));
    */

    UINT flag = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    if (requireVideo)
        flag |= D3D11_CREATE_DEVICE_VIDEO_SUPPORT;

#if !defined(NDEBUG) && defined(TWILIGHT_D3D_DEBUG)
    flag |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_10_0};
    hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flag, featureLevels, 1, D3D11_SDK_VERSION,
                           device.data(), nullptr, nullptr);

    if (FAILED(hr)) {
        DXGI_ADAPTER_DESC desc;
        adapter->GetDesc(&desc);

        log.warn("Failed to create D3D device for {} (requireVideo={})", intoUTF8(desc.Description), requireVideo);
        device.release();
        if (releaseAdapter)
            adapter->Release();
        return device;
    }

    if (requireVideo)
        device.castTo<ID3D10Multithread>()->SetMultithreadProtected(true);

    if (releaseAdapter)
        adapter->Release();
    return device;
}

DxgiAdapter1 DxgiHelper::getAdapterFromOutput(const DxgiOutput5 &output) {
    HRESULT hr;
    DxgiAdapter1 adapter;

    hr = output->GetParent(adapter.guid(), adapter.data());
    if (FAILED(hr)) {
        log.error("Failed to get adapter from output (GUID={})", intoString(output.guid()));
        adapter.release();
    }
    return adapter;
}
