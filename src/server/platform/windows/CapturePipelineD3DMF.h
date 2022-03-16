#ifndef TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTUREPIPELINED3DMF_H
#define TWILIGHT_SERVER_PLATFORM_WINDOWS_CAPTUREPIPELINED3DMF_H

#include "common/log.h"

#include "common/platform/windows/DxgiHelper.h"
#include "common/platform/windows/QPCTimer.h"

#include "server/CapturePipeline.h"

#include "server/platform/windows/CaptureD3D.h"
#include "server/platform/windows/EncoderMF.h"
#include "server/platform/windows/ScaleD3D.h"

#include <thread>

class CapturePipelineD3DMF : public CapturePipeline {
public:
    CapturePipelineD3DMF(LocalClock& clock, DxgiHelper dxgiHelper);
    ~CapturePipelineD3DMF() override;

    bool init() override;

    void start() override;
    void stop() override;

    void getNativeMode(int* width, int* height, Rational* framerate) override;

    bool setCaptureMode(int width, int height, Rational framerate) override;
    bool setEncoderMode(int width, int height, Rational framerate) override;

private:
    LoggerPtr log;

    std::thread captureThread;
    std::thread encodeThread;

    DxgiHelper dxgiHelper;
    D3D11Device device;
    D3D11DeviceContext context;

    D3D11Texture2D captureStagingTex;
    DxgiKeyedMutex captureStagingTexMutex;
    HANDLE captureStagingTexHandle;

    Rational framerate;

    std::atomic<bool> flagRunning;

    std::mutex frameLock;
    DesktopFrame<bool> lastFrame;

    QPCTimer timer;
    CaptureD3D capture;
    std::unique_ptr<ScaleD3D> scale;
    EncoderMF encoder;

    void captureLoop_();
    void encodeLoop_();
};

#endif