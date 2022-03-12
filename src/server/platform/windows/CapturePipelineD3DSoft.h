#ifndef TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTUREPIPELINED3DSOFT_H
#define TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTUREPIPELINED3DSOFT_H

#include "common/log.h"

#include "common/platform/software/ScaleSoftware.h"

#include "common/platform/windows/QPCTimer.h"

#include "server/CapturePipeline.h"

#include "server/platform/software/EncoderSoftware.h"

#include "server/platform/windows/CaptureD3D.h"

#include <atomic>
#include <thread>

class CapturePipelineD3DSoft : public CapturePipeline {
public:
    explicit CapturePipelineD3DSoft(DxgiHelper dxgiHelper);
    ~CapturePipelineD3DSoft() override;

    bool init() override;

    void start() override;
    void stop() override;

    void getNativeMode(int* width, int* height, Rational* framerate) override;

    bool setCaptureMode(int width, int height, Rational framerate) override;
    bool setEncoderMode(int width, int height, Rational framerate) override;

private:
    LoggerPtr log;
    ScaleType scaleType;

    DxgiHelper dxgiHelper;
    CaptureD3D capture;
    ScaleSoftware scale;
    EncoderSoftware encoder;

    Rational framerate;
    QPCTimer timer;

    std::thread captureThread;
    std::thread encodeThread;
    std::atomic<bool> flagRun;

    std::mutex frameLock;
    DesktopFrame<bool> lastFrame;

    void loopCapture_();
    void loopEncoder_();
};

#endif