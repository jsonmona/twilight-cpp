#include "CaptureD3D.h"

#include "common/platform/windows/winheaders.h"

#include <cassert>
#include <deque>
#include <utility>

static long long getPerformanceFreqency() {
    static_assert(sizeof(LARGE_INTEGER::QuadPart) == sizeof(long long), "long long must be compatible with QuadPart");

    LARGE_INTEGER a;
    QueryPerformanceFrequency(&a);
    return a.QuadPart;
}

static long long getPerformanceCounter() {
    LARGE_INTEGER a;
    QueryPerformanceCounter(&a);
    return a.QuadPart;
}

CaptureD3D::CaptureD3D(DeviceManagerD3D _devs)
    : log(createNamedLogger("CaptureD3D")),
      output(_devs.output),
      device(_devs.device),
      context(_devs.context),
      statMixer(120) {
    perfCounterFreq = getPerformanceFreqency();
}

CaptureD3D::~CaptureD3D() {}

void CaptureD3D::start() {
    timeBeginPeriod(2);

    firstFrameSent = false;

    // TODO: Add timeout
    while (!openDuplication_())
        Sleep(1);

    check_quit(frameInterval <= 0, log, "Framerate not set before start() is called");
    lastPresentTime = getPerformanceCounter() - frameInterval;
}

void CaptureD3D::stop() {
    if (frameAcquired) {
        HRESULT hr = outputDuplication->ReleaseFrame();
        if (hr != DXGI_ERROR_ACCESS_LOST)
            check_quit(FAILED(hr), log, "Failed to release frame ({})", hr);
        frameAcquired = false;
    }

    outputDuplication.release();

    timeEndPeriod(2);
}

void CaptureD3D::poll() {
    captureFrame_();

    long long nowTime = getPerformanceCounter();
    if (lastPresentTime + frameInterval <= nowTime) {
        statMixer.pushValue(static_cast<float>(nowTime - lastPresentTime) / perfCounterFreq);
        lastPresentTime = nowTime;

        DesktopFrame<D3D11Texture2D> nextFrame;
        if (desktopTexDirty)
            nextFrame.desktop = std::make_shared<D3D11Texture2D>(nextDesktop);
        nextFrame.cursorPos = std::move(nextCursorPos);
        nextFrame.cursorShape = std::move(nextCursorShape);

        onNextFrame(std::move(nextFrame));
    }
}

void CaptureD3D::setFramerate(Rational framerate) {
    frameInterval = framerate.inv().imul(perfCounterFreq);  // perfCounterFreq / framerate
}

void CaptureD3D::getCurrentMode(int* width, int* height, Rational* framerate) {
    // TODO: Add timeout (or make it async?)
    while (!openDuplication_())
        Sleep(1);

    DXGI_OUTDUPL_DESC desc;
    outputDuplication->GetDesc(&desc);

    *width = desc.ModeDesc.Width;
    *height = desc.ModeDesc.Height;
    *framerate = Rational(desc.ModeDesc.RefreshRate.Numerator, desc.ModeDesc.RefreshRate.Denominator);
}

bool CaptureD3D::tryReleaseFrame_() {
    HRESULT hr;

    if (outputDuplication.isInvalid() || !frameAcquired)
        return true;

    frameAcquired = false;
    hr = outputDuplication->ReleaseFrame();
    if (SUCCEEDED(hr))
        return true;
    if (hr == DXGI_ERROR_ACCESS_LOST)
        return openDuplication_();
    else
        check_quit(FAILED(hr), log, "Failed to release frame ({})", hr);

    return false;
}

bool CaptureD3D::openDuplication_() {
    HRESULT hr;

    if (outputDuplication.isValid())
        outputDuplication.release();

    frameAcquired = false;

    DXGI_FORMAT supportedFormats[] = {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM};
    hr = output->DuplicateOutput1(device.ptr(), 0, 2, supportedFormats, outputDuplication.data());
    if (SUCCEEDED(hr)) {
        DXGI_OUTDUPL_DESC desc;
        outputDuplication->GetDesc(&desc);

        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = desc.ModeDesc.Width;
        texDesc.Height = desc.ModeDesc.Height;
        texDesc.Format = desc.ModeDesc.Format;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        nextDesktop.release();
        hr = device->CreateTexture2D(&texDesc, nullptr, nextDesktop.data());
        check_quit(FAILED(hr), log, "Failed to staging texture");
    } else if (hr == E_ACCESSDENIED) {
        Sleep(1);
        return false;
    } else
        check_quit(FAILED(hr), log, "Failed to duplicate output ({:#x})", hr);

    return true;
}

void CaptureD3D::captureFrame_() {
    HRESULT hr;

    // Access denied (secure desktop, etc.)
    // TODO: Show black screen instead of last image
    if (outputDuplication.isInvalid() && !openDuplication_())
        return;

    if (!tryReleaseFrame_())
        return;

    DxgiResource desktopResource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    hr = outputDuplication->AcquireNextFrame(0, &frameInfo, desktopResource.data());
    if (SUCCEEDED(hr)) {
        frameAcquired = true;

        if (frameInfo.LastPresentTime.QuadPart != 0 || !firstFrameSent) {
            firstFrameSent = true;
            desktopTexDirty = true;
            D3D11Texture2D rgbTex = desktopResource.castTo<ID3D11Texture2D>();
            context->CopyResource(nextDesktop.ptr(), rgbTex.ptr());
        }

        if (frameInfo.LastMouseUpdateTime.QuadPart != 0) {
            if (!nextCursorPos)
                nextCursorPos = std::make_shared<CursorPos>();

            nextCursorPos->visible = frameInfo.PointerPosition.Visible;
            if (frameInfo.PointerPosition.Visible) {
                nextCursorPos->x = frameInfo.PointerPosition.Position.x;
                nextCursorPos->y = frameInfo.PointerPosition.Position.y;
            }
        }

        if (frameInfo.PointerShapeBufferSize != 0) {
            if (!nextCursorShape)
                nextCursorShape = std::make_shared<CursorShape>();

            UINT bufferSize = frameInfo.PointerShapeBufferSize;
            std::vector<uint8_t> buffer(bufferSize);

            DXGI_OUTDUPL_POINTER_SHAPE_INFO cursorInfo;
            hr = outputDuplication->GetFramePointerShape(bufferSize, buffer.data(), &bufferSize, &cursorInfo);
            check_quit(FAILED(hr), log, "Failed to fetch frame pointer shape");

            nextCursorShape->hotspotX = cursorInfo.HotSpot.x;
            nextCursorShape->hotspotY = cursorInfo.HotSpot.y;
            parseCursor_(nextCursorShape.get(), cursorInfo, buffer);
        }
    } else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        // ignore
    } else if (hr == DXGI_ERROR_ACCESS_LOST)
        openDuplication_();
    else
        error_quit(log, "Failed to acquire next frame ({})", hr);
}

void CaptureD3D::parseCursor_(CursorShape* cursorShape, const DXGI_OUTDUPL_POINTER_SHAPE_INFO& cursorInfo,
                              const std::vector<uint8_t>& buffer) {
    if (cursorInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
        cursorShape->image.resize(cursorInfo.Height * cursorInfo.Width * 4);
        cursorShape->width = cursorInfo.Width;
        cursorShape->height = cursorInfo.Height;

        uint8_t* const basePtr = cursorShape->image.data();
        for (int i = 0; i < cursorInfo.Height; i++) {
            for (int j = 0; j < cursorInfo.Width; j++) {
                // bgra -> rgba
                uint32_t val = *reinterpret_cast<const uint32_t*>(buffer.data() + (i * cursorInfo.Pitch + j * 4));
                val = ((val & 0x00FF00FF) << 16) | ((val & 0x00FF00FF) >> 16) | (val & 0xFF00FF00);
                *reinterpret_cast<uint32_t*>(basePtr + (i * cursorInfo.Width * 4 + j * 4)) = val;
            }
        }
    } else if (cursorInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME) {
        cursorShape->image.resize(cursorInfo.Height * cursorInfo.Width * 4 / 2);
        cursorShape->width = cursorInfo.Width;
        cursorShape->height = cursorInfo.Height / 2;

        uint8_t* const basePtr = cursorShape->image.data();
        for (int i = 0; i < cursorInfo.Height / 2; i++) {
            for (int j = 0; j < cursorInfo.Width / 8; j++) {
                uint8_t value = buffer[i * cursorInfo.Pitch + j];
                uint8_t alpha = buffer[(i + cursorInfo.Height / 2) * cursorInfo.Pitch + j];
                for (int k = 0; k < 8; k++) {
                    uint8_t rgbValue = (value & 1) ? 0xFF : 0x00;
                    uint8_t alphaValue = (alpha & 1) ? 0xFF : 0x00;
                    value >>= 1;
                    alpha >>= 1;

                    basePtr[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4] = rgbValue;
                    basePtr[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4 + 1] = rgbValue;
                    basePtr[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4 + 2] = rgbValue;
                    basePtr[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4 + 3] = alphaValue;
                }
            }
        }
    } else {
        log->warn("Unknown cursor type: {}", cursorInfo.Type);
        cursorShape->image.resize(0);
        cursorShape->height = 0;
        cursorShape->width = 0;
    }
}
