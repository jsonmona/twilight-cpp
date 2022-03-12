#ifndef TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTURED3D_H
#define TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTURED3D_H

#include "common/DesktopFrame.h"
#include "common/Rational.h"
#include "common/StatisticMixer.h"
#include "common/log.h"

#include "common/platform/windows/ComWrapper.h"
#include "common/platform/windows/DxgiHelper.h"

#include "server/platform/windows/CaptureWin32.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

class CaptureD3D : public CaptureWin32 {
public:
    CaptureD3D();
    ~CaptureD3D() override;

    bool init(DxgiHelper dxgiHelper) override;
    bool open(DxgiOutput output) override;
    bool start() override;
    void stop() override;

    void getCurrentMode(int* width, int* height, Rational* framerate) override;

    DesktopFrame<TextureSoftware> readSoftware() override;
    DesktopFrame<D3D11Texture2D> readD3D() override;

    const D3D11DeviceContext& ctx() const { return context; }

private:
    LoggerPtr log;

    bool frameAcquired;
    bool sentFirstFrame;
    bool supportsMapping;

    DxgiHelper dxgiHelper;
    DxgiOutput5 output;
    D3D11Device device;
    D3D11DeviceContext context;
    DxgiOutputDuplication outputDuplication;

    bool tryReleaseFrame_();
    bool openDuplication_();
    void parseCursor_(CursorShape* cursorShape, const DXGI_OUTDUPL_POINTER_SHAPE_INFO& cursorInfo,
                      const std::vector<uint8_t>& buffer);
};

#endif