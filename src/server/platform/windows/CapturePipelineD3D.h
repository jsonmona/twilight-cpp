#ifndef TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTUREPIPELINED3D_H
#define TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTUREPIPELINED3D_H

#include "common/log.h"

#include "common/platform/windows/DeviceManagerD3D.h"

#include "CaptureD3D.h"
#include "EncoderD3D.h"
#include "ScaleD3D.h"
#include "server/CapturePipeline.h"

class CapturePipelineD3D : public CapturePipeline {
public:
    CapturePipelineD3D(DeviceManagerD3D devs_, ScaleType type);
    ~CapturePipelineD3D() override;

    void start() override;
    void stop() override;

    void getNativeMode(int* width, int* height, Rational* framerate) override;

    void setMode(int width, int height, Rational framerate) override;

    StatisticMixer::Stat calcCaptureStat() override { return capture.calcCaptureStat(); }
    StatisticMixer::Stat calcEncoderStat() override { return encoder.calcEncoderStat(); }

private:
    LoggerPtr log;

    DeviceManagerD3D devs;
    ScaleType scaleType;
    CaptureD3D capture;
    std::unique_ptr<ScaleD3D> scale;
    EncoderD3D encoder;

    std::thread runThread;
    std::atomic<bool> flagRun;

    void captureNextFrame_(DesktopFrame<D3D11Texture2D>&& cap);
    void run_();
};

#endif