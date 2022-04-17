#ifndef TWILIGHT_COMMON_PLATFORM_WINDOWS_DXGIHELPER_H
#define TWILIGHT_COMMON_PLATFORM_WINDOWS_DXGIHELPER_H

#include "common/log.h"

#include "common/platform/windows/ComWrapper.h"

class DxgiHelper {
public:
    DxgiHelper();
    ~DxgiHelper();

    DxgiFactory5 getFactory() { return factory; }

    std::vector<DxgiOutput5> findAllOutput();

    D3D11Device createDevice(IDXGIAdapter* adapter, bool requireVideo);
    DxgiAdapter1 getAdapterFromOutput(const DxgiOutput5& output);

private:
    static NamedLogger log;

    DxgiFactory5 factory;
};

#endif
