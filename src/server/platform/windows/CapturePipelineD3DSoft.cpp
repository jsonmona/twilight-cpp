#include "CapturePipelineD3DSoft.h"

static AVPixelFormat dxgi2avpixfmt(DXGI_FORMAT fmt) {
    switch (fmt) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return AV_PIX_FMT_BGRA;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return AV_PIX_FMT_RGBA;
    default:
        error_quit(createNamedLogger("CapturePipelineD3DSoft"), "No matching AVPixelFormat for DXGI_FORMAT {}", fmt);
    }
}

static AVPixelFormat scale2avpixfmt(ScaleType type) {
    switch (type) {
    case ScaleType::AYUV:
        return AV_PIX_FMT_YUV444P;
    case ScaleType::NV12:
        return AV_PIX_FMT_YUV420P;
    default:
        error_quit(createNamedLogger("CapturePipelineD3DSoft"), "No matching AVPixelFormat for ScaleType {}", type);
    }
}

CapturePipelineD3DSoft::CapturePipelineD3DSoft(DeviceManagerD3D devs_, ScaleType type)
    : log(createNamedLogger("CapturePipelineD3DSoft")),
      scaleType(type),
      capture(devs_),
      scale(),
      encoder(),
      device(devs_.device),
      context(devs_.context) {}

CapturePipelineD3DSoft::~CapturePipelineD3DSoft() {}

void CapturePipelineD3DSoft::start() {
    lastPresentTime = std::chrono::steady_clock::now();

    capture.setOnNextFrame([this](DesktopFrame<D3D11Texture2D>&& cap) { captureNextFrame_(std::move(cap)); });
    encoder.setDataAvailableCallback(writeOutput);

    capture.start();
    encoder.start();

    flagRun.store(true, std::memory_order_release);
    runThread = std::thread([this]() { run_(); });
}

void CapturePipelineD3DSoft::stop() {
    flagRun.store(false, std::memory_order_relaxed);
    runThread.join();

    encoder.stop();
    capture.stop();
}

void CapturePipelineD3DSoft::getNativeMode(int* width, int* height, Rational* framerate) {
    capture.getCurrentMode(width, height, framerate);
}

void CapturePipelineD3DSoft::setMode(int width, int height, Rational framerate) {
    capture.setFramerate(framerate);
    scale.setOutputFormat(width, height, scale2avpixfmt(scaleType));

    // FIXME: Ugly code
    encoder.~EncoderSoftware();
    new (&encoder) EncoderSoftware();
}

void CapturePipelineD3DSoft::run_() {
    HRESULT hr;
    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    check_quit(FAILED(hr), log, "Failed to initialize COM");

    while (flagRun.load(std::memory_order_acquire)) {
        capture.poll();
    }

    CoUninitialize();
}

D3D11_TEXTURE2D_DESC CapturePipelineD3DSoft::copyToStageTex_(const D3D11Texture2D& tex) {
    D3D11_TEXTURE2D_DESC stageDesc;
    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);

    bool shouldRecreateTex = true;

    if (stageTex.isValid()) {
        stageTex->GetDesc(&stageDesc);

        shouldRecreateTex =
            (stageDesc.Format != desc.Format) || (stageDesc.Width != desc.Width) || (stageDesc.Height != desc.Height);
    }

    if (shouldRecreateTex) {
        stageTex.release();

        stageDesc = {};
        stageDesc.Format = desc.Format;
        stageDesc.Width = desc.Width;
        stageDesc.Height = desc.Height;
        stageDesc.Usage = D3D11_USAGE_STAGING;
        stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stageDesc.MipLevels = 1;
        stageDesc.ArraySize = 1;
        stageDesc.SampleDesc.Count = 1;

        device->CreateTexture2D(&stageDesc, nullptr, stageTex.data());
    }

    context->CopyResource(stageTex.ptr(), tex.ptr());
    return stageDesc;
}

void CapturePipelineD3DSoft::captureNextFrame_(DesktopFrame<D3D11Texture2D>&& cap) {
    if (cap.desktop) {
        D3D11_TEXTURE2D_DESC desc = copyToStageTex_(*cap.desktop);

        AVPixelFormat fmt = dxgi2avpixfmt(desc.Format);
        scale.setInputFormat(desc.Width, desc.Height, fmt);

        D3D11_MAPPED_SUBRESOURCE mapInfo;
        context->Map(stageTex.ptr(), 0, D3D11_MAP_READ, 0, &mapInfo);

        uint8_t* dataPtr = reinterpret_cast<uint8_t*>(mapInfo.pData);
        int linesize = mapInfo.RowPitch;

        TextureSoftware mappedTex = TextureSoftware::reference(&dataPtr, &linesize, desc.Width, desc.Height, fmt);
        scale.pushInput(std::move(mappedTex));
        scale.flush();

        context->Unmap(stageTex.ptr(), 0);

        lastTex = std::make_shared<TextureSoftware>(scale.popOutput());
    }

    DesktopFrame<TextureSoftware> data;
    data.desktop = lastTex;
    data.cursorPos = std::move(cap.cursorPos);
    data.cursorShape = std::move(cap.cursorShape);
    encoder.pushData(std::move(data));
}
