#ifndef TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTUREWIN32_H
#define TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTUREWIN32_H

#include "common/DesktopFrame.h"

#include "common/platform/software/TextureSoftware.h"

#include "common/platform/windows/DxgiHelper.h"

class CaptureWin32 {
public:
    CaptureWin32() = default;
    CaptureWin32(const CaptureWin32& copy) = delete;
    CaptureWin32(CaptureWin32&& move) = delete;

    virtual ~CaptureWin32() = default;

    virtual bool init(DxgiHelper dxgiHelper) = 0;
    virtual bool open(DxgiOutput output) = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;

    virtual void getCurrentMode(int* width, int* height, Rational* framerate) = 0;

    virtual DesktopFrame<TextureSoftware> readSoftware() = 0;
    virtual DesktopFrame<D3D11Texture2D> readD3D() = 0;
};

#endif