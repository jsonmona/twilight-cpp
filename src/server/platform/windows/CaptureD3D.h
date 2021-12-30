#ifndef TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTURED3D_H
#define TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTURED3D_H

#include "common/DesktopFrame.h"
#include "common/StatisticMixer.h"
#include "common/log.h"

#include "common/platform/windows/ComWrapper.h"
#include "common/platform/windows/DeviceManagerD3D.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

class CaptureD3D {
    LoggerPtr log;

    int fps;
    bool frameAcquired;
    bool firstFrameSent;
    bool desktopTexDirty;

    DxgiOutput5 output;
    D3D11Device device;
    D3D11DeviceContext context;
    DxgiOutputDuplication outputDuplication;

    D3D11Texture2D nextDesktop;
    std::shared_ptr<CursorPos> nextCursorPos;
    std::shared_ptr<CursorShape> nextCursorShape;

    long long perfCounterFreq;
    long long lastPresentTime;
    long long frameInterval;
    StatisticMixer statMixer;

    std::function<void(DesktopFrame<D3D11Texture2D>&&)> onNextFrame;

    bool tryReleaseFrame_();
    bool openDuplication_();
    void captureFrame_();
    void parseCursor_(CursorShape* cursorShape, const DXGI_OUTDUPL_POINTER_SHAPE_INFO& cursorInfo,
                      const std::vector<uint8_t>& buffer);

public:
    CaptureD3D(DeviceManagerD3D _devs);
    ~CaptureD3D();

    void start(int fps);
    void stop();

    void poll();

    StatisticMixer::Stat calcCaptureStat() { return statMixer.calcStat(); }

    template <typename Fn>
    void setOnNextFrame(Fn fn) {
        onNextFrame = std::move(fn);
    }
};

#endif