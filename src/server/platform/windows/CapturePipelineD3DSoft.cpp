#include "CapturePipelineD3DSoft.h"

TWILIGHT_DEFINE_LOGGER(CapturePipelineD3DSoft);

static AVPixelFormat dxgi2avpixfmt(DXGI_FORMAT fmt) {
    switch (fmt) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return AV_PIX_FMT_BGRA;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return AV_PIX_FMT_RGBA;
    default:
        NamedLogger("CapturePipelineD3DSoft").error_quit("No matching AVPixelFormat for DXGI_FORMAT {}", fmt);
    }
}

static AVPixelFormat scale2avpixfmt(ScaleType type) {
    switch (type) {
    case ScaleType::AYUV:
        return AV_PIX_FMT_YUV444P;
    case ScaleType::NV12:
        return AV_PIX_FMT_YUV420P;
    default:
        NamedLogger("CapturePipelineD3DSoft").error_quit("No matching AVPixelFormat for ScaleType {}", type);
    }
}

CapturePipelineD3DSoft::CapturePipelineD3DSoft(LocalClock& clock, DxgiHelper dxgiHelper)
    : dxgiHelper(dxgiHelper), scaleType(ScaleType::NV12), flagRun(false), capture(clock), encoder(clock) {}

CapturePipelineD3DSoft::~CapturePipelineD3DSoft() {}

bool CapturePipelineD3DSoft::init() {
    auto outputs = dxgiHelper.findAllOutput();
    log.assert_quit(!outputs.empty(), "No output available!");

    capture.init(dxgiHelper);
    capture.open(outputs[0].castTo<IDXGIOutput>());

    return true;
}

void CapturePipelineD3DSoft::start() {
    bool wasRunning = flagRun.exchange(true, std::memory_order_acq_rel);
    log.assert_quit(!wasRunning, "Starting without stopping first!");

    if (captureThread.joinable())
        captureThread.join();
    if (encodeThread.joinable())
        encodeThread.join();

    capture.start();
    encoder.start();

    timer.setFrequency(framerate);

    captureThread = std::thread([this]() { loopCapture_(); });
    encodeThread = std::thread([this]() { loopEncoder_(); });
}

void CapturePipelineD3DSoft::stop() {
    bool wasRunning = flagRun.exchange(false, std::memory_order_acq_rel);
    log.assert_quit(wasRunning, "Stopping when not running!");

    encoder.stop();
    capture.stop();
}

void CapturePipelineD3DSoft::getNativeMode(int* width, int* height, Rational* framerate) {
    capture.getCurrentMode(width, height, framerate);
}

bool CapturePipelineD3DSoft::setCaptureMode(int width, int height, Rational framerate) {
    return false;
}

bool CapturePipelineD3DSoft::setEncoderMode(int width, int height, Rational framerate) {
    scale.setOutputFormat(width, height, scale2avpixfmt(scaleType));
    encoder.setMode(width, height, framerate);

    this->framerate = framerate;

    return true;
}

void CapturePipelineD3DSoft::loopCapture_() {
    StatisticMixer mixer(120);
    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point p = std::chrono::steady_clock::now();
    while (flagRun.load(std::memory_order_acquire)) {
        DesktopFrame<TextureSoftware> frame = capture.readSoftware();

        bool dirty = !frame.desktop.isEmpty() || frame.cursorPos || frame.cursorShape;

        if (dirty) {
            std::lock_guard lock(frameLock);

            if (!frame.desktop.isEmpty()) {
                scale.pushInput(std::move(frame.desktop));
                scale.flush();

                lastFrame.desktop = true;
                lastFrame.timeCaptured = frame.timeCaptured;
            }

            if (frame.cursorPos)
                lastFrame.cursorPos = std::move(frame.cursorPos);
            if (frame.cursorShape)
                lastFrame.cursorShape = std::move(frame.cursorShape);
        }
    }
}

void CapturePipelineD3DSoft::loopEncoder_() {
    using std::swap;

    bool firstFrameProvided = false;

    while (flagRun.load(std::memory_order_acquire)) {
        while (!timer.checkInterval() && flagRun.load(std::memory_order_relaxed))
            Sleep(1);

        DesktopFrame<TextureSoftware> frame;

        /* load desktop frame */ {
            std::lock_guard lock(frameLock);
            if (!firstFrameProvided && !lastFrame.desktop)
                continue;

            firstFrameProvided = true;
            frame = lastFrame.getOtherType(scale.popOutput());
            lastFrame.desktop = false;
        }

        encoder.pushFrame(std::move(frame));
        DesktopFrame<ByteBuffer> output;
        if (encoder.readData(&output))
            writeOutput(std::move(output));
    }
}
