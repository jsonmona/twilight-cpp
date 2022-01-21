#include "CapturePipelineD3D.h"

CapturePipelineD3D::CapturePipelineD3D(DeviceManagerD3D devs_, ScaleType type)
    : log(createNamedLogger("CapturePipelineD3D")), devs(devs_), scaleType(type), capture(devs_), encoder(devs_) {}

CapturePipelineD3D::~CapturePipelineD3D() {
    if (runThread.joinable())
        runThread.join();
}

void CapturePipelineD3D::start() {
    capture.setOnNextFrame([this](DesktopFrame<D3D11Texture2D>&& cap) { captureNextFrame_(std::move(cap)); });
    encoder.setOnDataAvailable(writeOutput);

    if (runThread.joinable())
        runThread.join();

    encoder.start();
    capture.start();

    flagRun.store(true, std::memory_order_release);
    runThread = std::thread([this]() { run_(); });
}

void CapturePipelineD3D::stop() {
    flagRun.exchange(false, std::memory_order_release);
    // runThread.join();
    // FIXME: Can't join here because runThread may be the one calling stop; I don't like it

    capture.stop();
    encoder.stop();
}

void CapturePipelineD3D::getNativeMode(int* width, int* height, Rational* framerate) {
    capture.getCurrentMode(width, height, framerate);
}

void CapturePipelineD3D::setMode(int width, int height, Rational framerate) {
    capture.setFramerate(framerate);
    scale = ScaleD3D::createInstance(width, height, scaleType, false);
    scale->init(devs.device, devs.context);

    // FIXME: Ugly code
    encoder.~EncoderD3D();
    new (&encoder) EncoderD3D(devs);
}

void CapturePipelineD3D::run_() {
    HRESULT hr;
    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    check_quit(FAILED(hr), log, "Failed to initialize COM");

    while (flagRun.load(std::memory_order_acquire)) {
        capture.poll();
        encoder.poll();
    }

    CoUninitialize();
}

void CapturePipelineD3D::captureNextFrame_(DesktopFrame<D3D11Texture2D>&& cap) {
    if (cap.desktop)
        scale->pushInput(*cap.desktop);

    DesktopFrame<D3D11Texture2D> data;
    data.desktop = std::make_shared<D3D11Texture2D>(scale->popOutput());
    data.cursorPos = std::move(cap.cursorPos);
    data.cursorShape = std::move(cap.cursorShape);
    encoder.pushFrame(std::move(data));
}
