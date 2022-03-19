#include "CaptureD3D.h"

#include "CaptureWin32.h"

#include "common/platform/windows/winheaders.h"

#include <cassert>
#include <deque>
#include <utility>

static uint64_t qpc() {
    LARGE_INTEGER val;
    QueryPerformanceCounter(&val);
    return val.QuadPart;
}

static uint64_t getQpcFreq() {
    LARGE_INTEGER val;
    QueryPerformanceFrequency(&val);
    return val.QuadPart;
}

CaptureD3D::CaptureD3D(LocalClock& clock)
    : log(createNamedLogger("CaptureD3D")),
      frameAcquired(false),
      sentFirstFrame(false),
      supportsMapping(false),
      clock(clock),
      qpcFreq(getQpcFreq()) {}

CaptureD3D::~CaptureD3D() {}

bool CaptureD3D::init(DxgiHelper dxgiHelper_) {
    dxgiHelper = std::move(dxgiHelper_);

    output.release();
    device.release();
    context.release();
    outputDuplication.release();

    frameAcquired = false;
    sentFirstFrame = false;
    supportsMapping = false;

    return true;
}

bool CaptureD3D::open(DxgiOutput output_) {
    output = output_.castTo<IDXGIOutput5>();
    if (output.isInvalid())
        return false;

    device = dxgiHelper.createDevice(dxgiHelper.getAdapterFromOutput(output).ptr(), false);
    check_quit(device.isInvalid(), log, "Failed to create D3D device from output");

    context.release();
    device->GetImmediateContext(context.data());
    check_quit(context.isInvalid(), log, "Failed to get immediate context");

    auto oldTime = std::chrono::steady_clock::now();
    while (!openDuplication_()) {
        if (std::chrono::steady_clock::now() - oldTime > std::chrono::milliseconds(5000)) {
            log->warn("Failed to open duplication before timeout");
            return false;
        }
    }

    return true;
}

bool CaptureD3D::start() {
    timeBeginPeriod(1);

    sentFirstFrame = false;

    return true;
}

void CaptureD3D::stop() {
    if (frameAcquired) {
        HRESULT hr = outputDuplication->ReleaseFrame();
        if (hr != DXGI_ERROR_ACCESS_LOST)
            check_quit(FAILED(hr), log, "Failed to release frame ({})", hr);
        frameAcquired = false;
    }

    outputDuplication.release();

    timeEndPeriod(1);
}

void CaptureD3D::getCurrentMode(int* width, int* height, Rational* framerate) {
    check_quit(output.isInvalid(), log, "Queried current mode before open");

    // TODO: Add timeout (or make it async?)
    while (!openDuplication_())
        Sleep(1);

    DXGI_OUTDUPL_DESC desc;
    outputDuplication->GetDesc(&desc);

    if (width)
        *width = desc.ModeDesc.Width;
    if (height)
        *height = desc.ModeDesc.Height;
    if (framerate)
        *framerate = Rational(desc.ModeDesc.RefreshRate.Numerator, desc.ModeDesc.RefreshRate.Denominator);
}

DesktopFrame<TextureSoftware> CaptureD3D::readSoftware() {
    HRESULT hr;
    DesktopFrame<D3D11Texture2D> frame = readD3D();
    TextureSoftware tex;

    // TODO: Cache staging texture
    if (frame.desktop.isValid()) {
        D3D11Texture2D staging;

        D3D11_TEXTURE2D_DESC oldDesc = {};
        D3D11_TEXTURE2D_DESC desc = {};

        frame.desktop->GetDesc(&oldDesc);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Width = oldDesc.Width;
        desc.Height = oldDesc.Height;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.ArraySize = 1;
        desc.SampleDesc.Count = 1;
        desc.MipLevels = 1;

        hr = device->CreateTexture2D(&desc, nullptr, staging.data());
        check_quit(FAILED(hr), log, "Failed to create staging texture");

        context->CopyResource(staging.ptr(), frame.desktop.ptr());

        D3D11_MAPPED_SUBRESOURCE mapInfo;
        hr = context->Map(staging.ptr(), 0, D3D11_MAP_READ, 0, &mapInfo);
        check_quit(FAILED(hr), log, "Failed to map staging texture");

        void* data[4] = {mapInfo.pData, nullptr, nullptr, nullptr};
        int linesize[4] = {mapInfo.RowPitch, 0, 0, 0};
        tex = TextureSoftware::reference(reinterpret_cast<uint8_t**>(data), linesize, oldDesc.Width, oldDesc.Height,
                                         AV_PIX_FMT_BGRA)
                  .clone();

        context->Unmap(staging.ptr(), 0);
    }

    return frame.getOtherType<TextureSoftware>(std::move(tex));
}

DesktopFrame<D3D11Texture2D> CaptureD3D::readD3D() {
    HRESULT hr;
    DesktopFrame<D3D11Texture2D> frame;

    // Access denied (secure desktop, etc.)
    // TODO: Show black screen instead of last image
    if (outputDuplication.isInvalid() && !openDuplication_())
        return frame;

    if (!tryReleaseFrame_())
        return frame;

    DxgiResource desktopResource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    hr = outputDuplication->AcquireNextFrame(150, &frameInfo, desktopResource.data());

    if (SUCCEEDED(hr)) {
        frameAcquired = true;

        if (frameInfo.LastPresentTime.QuadPart != 0 || !sentFirstFrame) {
            sentFirstFrame = true;
            frame.desktop = desktopResource.castTo<ID3D11Texture2D>();

            // Represent present time in LocalClock's time
            uint64_t delay = qpc() - frameInfo.LastPresentTime.QuadPart;
            std::chrono::microseconds currTime = clock.time();

            // Optimize for 10MHz. MSVC devs do that, so it must be worth it.
            if (qpcFreq == 10'000'000)
                currTime -= std::chrono::microseconds(delay / 10);
            else
                currTime -= std::chrono::microseconds(delay * 1'000'000 / qpcFreq);

            frame.timeCaptured = currTime;
        }

        if (frameInfo.LastMouseUpdateTime.QuadPart != 0) {
            frame.cursorPos = std::make_shared<CursorPos>();

            frame.cursorPos->visible = frameInfo.PointerPosition.Visible;
            frame.cursorPos->xScaler = Rational(1, 1);
            frame.cursorPos->yScaler = Rational(1, 1);
            if (frameInfo.PointerPosition.Visible) {
                frame.cursorPos->x = frameInfo.PointerPosition.Position.x;
                frame.cursorPos->y = frameInfo.PointerPosition.Position.y;
            }
        }

        if (frameInfo.PointerShapeBufferSize != 0) {
            frame.cursorShape = std::make_shared<CursorShape>();

            UINT bufferSize = frameInfo.PointerShapeBufferSize;
            ByteBuffer buffer(bufferSize);

            DXGI_OUTDUPL_POINTER_SHAPE_INFO cursorInfo;
            hr = outputDuplication->GetFramePointerShape(bufferSize, buffer.data(), &bufferSize, &cursorInfo);
            check_quit(FAILED(hr), log, "Failed to fetch frame pointer shape");

            frame.cursorShape->hotspotX = cursorInfo.HotSpot.x;
            frame.cursorShape->hotspotY = cursorInfo.HotSpot.y;
            parseCursor_(frame.cursorShape.get(), cursorInfo, buffer);
        }
    } else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        // ignore
    } else if (hr == DXGI_ERROR_ACCESS_LOST)
        outputDuplication.release();
    else
        error_quit(log, "Failed to acquire next frame ({})", hr);

    return frame;
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

    outputDuplication.release();

    frameAcquired = false;
    sentFirstFrame = false;
    supportsMapping = false;

    DXGI_FORMAT supportedFormats[] = {DXGI_FORMAT_B8G8R8A8_UNORM};
    size_t supportedFormatsLen = sizeof(supportedFormats) / sizeof(DXGI_FORMAT);

    hr = output->DuplicateOutput1(device.ptr(), 0, supportedFormatsLen, supportedFormats, outputDuplication.data());
    if (hr == E_ACCESSDENIED)
        return false;
    else if (FAILED(hr))
        error_quit(log, "Failed to duplicate output ({:#x})", hr);

    DXGI_OUTDUPL_DESC desc = {};
    outputDuplication->GetDesc(&desc);

    supportsMapping = desc.DesktopImageInSystemMemory;
    return true;
}

void CaptureD3D::parseCursor_(CursorShape* cursorShape, const DXGI_OUTDUPL_POINTER_SHAPE_INFO& cursorInfo,
                              const ByteBuffer& buffer) {
    if (cursorInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR ||
        cursorInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR) {
        cursorShape->image.resize(cursorInfo.Height * cursorInfo.Width * 4);
        cursorShape->width = cursorInfo.Width;
        cursorShape->height = cursorInfo.Height;
        cursorShape->format = cursorInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR ? CursorShapeFormat::RGBA
                                                                                       : CursorShapeFormat::RGBA_XOR;

        uint32_t* dst = cursorShape->image.view<uint32_t>().data();
        for (int i = 0; i < cursorInfo.Height; i++) {
            for (int j = 0; j < cursorInfo.Width; j++) {
                // BGRA -> RGBA (0xAABBGGRR in little endian)
                uint32_t val = *reinterpret_cast<const uint32_t*>(buffer.data() + (i * cursorInfo.Pitch + j * 4));
                val = ((val & 0x00FF00FF) << 16) | ((val & 0x00FF00FF) >> 16) | (val & 0xFF00FF00);
                (*dst++) = val;
            }
        }
    } else if (cursorInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME) {
        cursorShape->image.resize(cursorInfo.Height / 2 * cursorInfo.Width * 4);
        cursorShape->width = cursorInfo.Width;
        cursorShape->height = cursorInfo.Height / 2;
        cursorShape->format = CursorShapeFormat::RGBA_XOR;

        uint32_t* dst = cursorShape->image.view<uint32_t>().data();
        for (int i = 0; i < cursorInfo.Height / 2; i++) {
            for (int j = 0; j < cursorInfo.Width / 8; j++) {
                uint8_t value = buffer[i * cursorInfo.Pitch + j];
                uint8_t alpha = buffer[(i + cursorInfo.Height / 2) * cursorInfo.Pitch + j];
                for (int k = 0; k < 8; k++) {
                    uint32_t rgb = (value & 0x80) ? 0xFF : 0x00;
                    uint32_t a = (alpha & 0x80) ? 0xFF : 0x00;
                    value <<= 1;
                    alpha <<= 1;

                    // RGBA (0xAABBGGRR in little endian)
                    (*dst++) = (a << 24) | (rgb << 16) | (rgb << 8) | rgb;
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
