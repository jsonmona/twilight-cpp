#include "CapturePipelineD3DMF.h"

CapturePipelineD3DMF::CapturePipelineD3DMF(DxgiHelper dxgiHelper)
    : log(createNamedLogger("CapturePipelineD3DMF")),
      dxgiHelper(dxgiHelper),
      captureStagingTexHandle(INVALID_HANDLE_VALUE) {}

CapturePipelineD3DMF::~CapturePipelineD3DMF() {
    check_quit(flagRunning.load(std::memory_order_relaxed), log, "Destructing without stopping first!");

    if (encodeThread.joinable())
        encodeThread.join();
    if (captureThread.joinable())
        captureThread.join();
    if (captureStagingTexHandle != INVALID_HANDLE_VALUE)
        CloseHandle(captureStagingTexHandle);
}

bool CapturePipelineD3DMF::init() {
    auto outputs = dxgiHelper.findAllOutput();
    if (outputs.empty()) {
        log->error("No display detected");
        return false;
    }

    DxgiOutput5 output = outputs[0];

    device = dxgiHelper.createDevice(dxgiHelper.getAdapterFromOutput(output), true);
    if (device.isInvalid()) {
        log->error("Failed to create video D3D11 context");
        return false;
    }

    context.release();
    device->GetImmediateContext(context.data());

    capture.init(dxgiHelper);
    capture.open(output.castTo<IDXGIOutput>());

    encoder.init(dxgiHelper);
    encoder.open(device, context);
    encoder.setOnDataAvailable(writeOutput);
}

void CapturePipelineD3DMF::start() {
    bool wasRunning = flagRunning.exchange(true, std::memory_order_relaxed);
    check_quit(wasRunning, log, "Starting again without stopping first!");

    if (captureThread.joinable())
        captureThread.join();
    if (encodeThread.joinable())
        encodeThread.join();
    if (captureStagingTexHandle != INVALID_HANDLE_VALUE)
        CloseHandle(captureStagingTexHandle);

    HRESULT hr;
    int w, h;
    Rational fps;
    capture.getCurrentMode(&w, &h, &fps);

    timer.setFrequency(fps);

    captureStagingTex.release();
    captureStagingTexMutex.release();

    scale->init(device, context);

    //FIXME: Does not support resolution change
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; //FIXME: Does not support HDR
    desc.Width = w;
    desc.Height = h;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.MipLevels = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    hr = device->CreateTexture2D(&desc, nullptr, captureStagingTex.data());
    check_quit(FAILED(hr), log, "Failed to create capture staging texture");

    captureStagingTexMutex = captureStagingTex.castTo<IDXGIKeyedMutex>();
    check_quit(captureStagingTexMutex.isInvalid(), log, "Failed to get keyed mutex from texture");

    hr = captureStagingTex.castTo<IDXGIResource1>()->CreateSharedHandle(
        nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &captureStagingTexHandle);
    check_quit(FAILED(hr), log, "Failed to get shared handle for capture staging texture");

    lastFrame = DesktopFrame<bool>();

    encoder.start();
    capture.start();

    captureThread = std::thread([this]() { captureLoop_(); });
    encodeThread = std::thread([this]() { encodeLoop_(); });
}

void CapturePipelineD3DMF::stop() {
    flagRunning.store(false, std::memory_order_relaxed);

    capture.stop();
    encoder.stop();
}

void CapturePipelineD3DMF::getNativeMode(int* width, int* height, Rational* framerate) {
    capture.getCurrentMode(width, height, framerate);
}

bool CapturePipelineD3DMF::setCaptureMode(int width, int height, Rational framerate) {
    return false;
}

bool CapturePipelineD3DMF::setEncoderMode(int width, int height, Rational framerate_) {
    //TODO: Make it possible to change mode while running
    check_quit(flagRunning, log, "Can't change mode while running yet");
    scale = ScaleD3D::createInstance(width, height, ScaleType::NV12);
    framerate = framerate_;
    return true;
}

void CapturePipelineD3DMF::captureLoop_() {
    HRESULT hr;

    D3D11Texture2D stagingTex;
    DxgiKeyedMutex stagingTexMutex;
    /* open shared texture */ {
        D3D11Device olddev;
        capture.ctx()->GetDevice(olddev.data());

        auto dev = olddev.castTo<ID3D11Device1>();
        check_quit(dev.isInvalid(), log, "Capture context device does not implement ID3D11Device1");

        hr = dev->OpenSharedResource1(captureStagingTexHandle, __uuidof(ID3D11Texture2D), stagingTex.data());
        check_quit(FAILED(hr), log, "Failed to open capture staging texture");

        stagingTexMutex = stagingTex.castTo<IDXGIKeyedMutex>();
        check_quit(stagingTexMutex.isInvalid(), log, "Failed to cast stagingTex into IDXGIKeyedMutex");
    }

    while (flagRunning.load(std::memory_order_acquire)) {
        DesktopFrame<D3D11Texture2D> frame = capture.readD3D();

        if (!flagRunning.load(std::memory_order_relaxed))
            continue;

        bool dirty = frame.desktop.isValid() || frame.cursorPos || frame.cursorShape;

        if (dirty) {
            std::lock_guard lock(frameLock);

            if (frame.desktop.isValid()) {
                // open mutex if needed
                hr = stagingTexMutex->AcquireSync(0, INFINITE);
                check_quit(FAILED(hr), log, "Failed to acquire sync for staging texture");

                capture.ctx()->CopyResource(stagingTex.ptr(), frame.desktop.ptr());

                hr = stagingTexMutex->ReleaseSync(0);
                check_quit(FAILED(hr), log, "Failed to release sync for staging texture");

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

void CapturePipelineD3DMF::encodeLoop_() {
    using std::swap;

    HRESULT hr;
    bool started = false;

    while (flagRunning.load(std::memory_order_relaxed)) {
        while (!timer.checkInterval() && flagRunning.load(std::memory_order_relaxed)) {
            encoder.poll();
            if (!timer.checkInterval())
                Sleep(1);
        }

        DesktopFrame<bool> frame;

        /* lock mutex */ {
            std::lock_guard lock(frameLock);
            if (!started && !lastFrame.desktop)
                continue;

            swap(frame, lastFrame);

            if (frame.desktop) {
                started = true;
                hr = captureStagingTexMutex->AcquireSync(0, INFINITE);
                check_quit(FAILED(hr), log, "Failed to acquire sync for staging texture");

                scale->pushInput(captureStagingTex);

                hr = captureStagingTexMutex->ReleaseSync(0);
                check_quit(FAILED(hr), log, "Failed to release sync for staging texture");
            }
        }

        encoder.poll();

        DesktopFrame<D3D11Texture2D> d3dFrame = frame.getOtherType(scale->popOutput());
        while (!encoder.pushFrame(&d3dFrame) && flagRunning.load(std::memory_order_relaxed))
            encoder.poll();
    }
}