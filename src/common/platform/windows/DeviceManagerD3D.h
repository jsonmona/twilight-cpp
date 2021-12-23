#ifndef SERVER_PLATFORM_WINDOWS_DEVICE_MANAGER_D3D_H_
#define SERVER_PLATFORM_WINDOWS_DEVICE_MANAGER_D3D_H_

#include "common/log.h"
#include "common/platform/windows/ComWrapper.h"

class DeviceManagerD3D {
    LoggerPtr log;

    bool hasVideoSupport = false;

public:
    DxgiFactory5 dxgiFactory;
    DxgiAdapter1 adapter;
    DxgiOutput5 output;
    D3D11Device device;
    D3D11DeviceContext context;

    // valid only if hasVideoEncoder
    MFDxgiDeviceManager mfDeviceManager;
    UINT mfDeviceManagerResetToken = 0;

public:
    DeviceManagerD3D();

    bool isVideoSupported();
};

#endif