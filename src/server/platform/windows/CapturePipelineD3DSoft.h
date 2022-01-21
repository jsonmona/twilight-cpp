#ifndef TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTUREPIPELINED3DSOFT_H
#define TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTUREPIPELINED3DSOFT_H

#include "common/log.h"

#include "common/platform/windows/DeviceManagerD3D.h"

#include "CaptureD3D.h"
#include "server/CapturePipeline.h"

#include "common/platform/software/ScaleSoftware.h"
#include "server/platform/software/EncoderSoftware.h"

#include <atomic>
#include <thread>

class CapturePipelineD3DSoft : public CapturePipeline {
public:
    CapturePipelineD3DSoft(DeviceManagerD3D devs_, ScaleType type);
    ~CapturePipelineD3DSoft() override;

    void start() override;
    void stop() override;

    void getNativeMode(int* width, int* height, Rational* framerate) override;

    void setMode(int width, int height, Rational framerate) override;

    StatisticMixer::Stat calcCaptureStat() override { return capture.calcCaptureStat(); }
    StatisticMixer::Stat calcEncoderStat() override { return encoder.calcEncoderStat(); }

private:
    LoggerPtr log;
    ScaleType scaleType;

    CaptureD3D capture;
    ScaleSoftware scale;
    EncoderSoftware encoder;

    D3D11Device device;
    D3D11DeviceContext context;
    D3D11Texture2D stageTex;
    std::shared_ptr<TextureSoftware> lastTex;

    std::chrono::steady_clock::time_point lastPresentTime;

    std::atomic<bool> flagRun;
    std::thread runThread;

    void run_();
    D3D11_TEXTURE2D_DESC copyToStageTex_(const D3D11Texture2D& tex);
    void captureNextFrame_(DesktopFrame<D3D11Texture2D>&& cap);
};

#endif