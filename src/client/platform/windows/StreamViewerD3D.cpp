#include "StreamViewerD3D.h"

#include "hlsl-viewer.h"

#include "common/StatisticMixer.h"
#include "common/util.h"

#include <vector>

TWILIGHT_DEFINE_LOGGER(StreamViewerD3D);

struct CursorBox_cbuffer {
    float cursorPos[2];
    float cursorSize[2];
    uint32_t flagCursorVisible;
    uint32_t flagCursorXOR;
    uint32_t dummy_[2];
};
static_assert(sizeof(CursorBox_cbuffer) % 16 == 0, "cbuffer size must be multiple of 16");

StreamViewerD3D::StreamViewerD3D(NetworkClock &clock)
    : StreamViewerBase(),
      clock(clock),
      flagInitialized(false),
      flagRunRender(false),
      width(-1),
      height(-1),
      cursorTexSize(-1) {}

StreamViewerD3D::~StreamViewerD3D() {
    bool wasInitialized = flagInitialized.load(std::memory_order_seq_cst);

    if (wasInitialized) {
        decoder->stop();

        flagRunRender.store(false, std::memory_order_release);
        renderThread.join();
    }
}

void StreamViewerD3D::resizeEvent(QResizeEvent *ev) {
    StreamViewerBase::resizeEvent(ev);

    bool wasInitialized = flagInitialized.exchange(true, std::memory_order_seq_cst);
    if (!wasInitialized) {
        init_();
    }
}

void StreamViewerD3D::setDrawCursor(bool newval) {}

void StreamViewerD3D::processDesktopFrame(const msg::Packet &pkt, uint8_t *extraData) {
    while (!flagRunRender.load(std::memory_order_acquire))
        Sleep(1);

    auto &res = pkt.desktop_frame();
    clock.monotonicHint(res.time_encoded());

    DesktopFrame<ByteBuffer> now;
    now.desktop.write(0, extraData, pkt.extra_data_len());

    now.timeCaptured = std::chrono::microseconds(res.time_captured());
    now.timeEncoded = std::chrono::microseconds(res.time_encoded());
    now.timeReceived = clock.time();

    now.isIDR = res.is_idr();

    now.cursorPos = std::make_shared<CursorPos>();
    now.cursorPos->visible = res.cursor_visible();
    if (now.cursorPos->visible) {
        now.cursorPos->x = res.cursor_x();
        now.cursorPos->y = res.cursor_y();
        now.cursorPos->xScaler = Rational(res.cursor_x_scaler_num(), res.cursor_x_scaler_den());
        now.cursorPos->yScaler = Rational(res.cursor_y_scaler_num(), res.cursor_y_scaler_den());
    } else {
        now.cursorPos->x = -1;
        now.cursorPos->y = -1;
    }

    now.cursorShape = std::atomic_exchange(&pendingCursorChange, {});

    decoder->pushData(std::move(now));
}

void StreamViewerD3D::processCursorShape(const msg::Packet &pkt, uint8_t *extraData) {
    const auto &data = pkt.cursor_shape();

    auto now = std::make_shared<CursorShape>();
    now->image.write(0, extraData, pkt.extra_data_len());
    now->height = data.height();
    now->width = data.width();
    now->hotspotX = data.hotspot_x();
    now->hotspotY = data.hotspot_y();

    switch (data.format()) {
    case msg::CursorShape_Format_RGBA:
        now->format = CursorShapeFormat::RGBA;
        break;
    case msg::CursorShape_Format_RGBA_XOR:
        now->format = CursorShapeFormat::RGBA_XOR;
        break;
    default:
        log.warn("Received unknown cursor shape format: {}", data.format());
        now->format = CursorShapeFormat::RGBA;
    }

    std::atomic_exchange(&pendingCursorChange, now);
}

void StreamViewerD3D::init_() {
    HRESULT hr;

    device = dxgiHelper.createDevice(nullptr, true);
    log.assert_quit(device.isValid(), "Failed to create D3D device");

    context.release();
    device->GetImmediateContext(context.data());

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = 0;
    swapChainDesc.Height = 0;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    hr = dxgiHelper.getFactory()->CreateSwapChainForHwnd(device.ptr(), hwnd(), &swapChainDesc, nullptr, nullptr,
                                                         swapChain.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create swap chain");

    hr = swapChain->GetDesc1(&swapChainDesc);
    log.assert_quit(SUCCEEDED(hr), "Failed to get swapchain desc");
    width = swapChainDesc.Width;
    height = swapChainDesc.Height;

    hr = device->CreateVertexShader(TWILIGHT_ARRAY_WITHLEN(g_vs_fullscreen), nullptr, vertexShaderFullscreen.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create vertex shader (full)");

    hr = device->CreatePixelShader(TWILIGHT_ARRAY_WITHLEN(g_ps_desktop), nullptr, pixelShaderDesktop.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create pixel shader");

    D3D11_BUFFER_DESC cbufferDesc = {};
    cbufferDesc.ByteWidth = sizeof(CursorBox_cbuffer);
    cbufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&cbufferDesc, nullptr, cbuffer.data());

    D3D11_TEXTURE2D_DESC desktopTexDesc = {};
    desktopTexDesc.Width = width;
    desktopTexDesc.Height = height;
    desktopTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desktopTexDesc.ArraySize = 1;
    desktopTexDesc.MipLevels = 1;
    desktopTexDesc.SampleDesc.Count = 1;
    desktopTexDesc.Usage = D3D11_USAGE_DYNAMIC;
    desktopTexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desktopTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = device->CreateTexture2D(&desktopTexDesc, nullptr, desktopTex.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to allocate desktop texture");

    D3D11_SHADER_RESOURCE_VIEW_DESC desktopSrvDesc = {};
    desktopSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    desktopSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desktopSrvDesc.Texture2D.MipLevels = 1;
    desktopSrvDesc.Texture2D.MostDetailedMip = 0;
    device->CreateShaderResourceView(desktopTex.ptr(), &desktopSrvDesc, desktopSRV.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create desktop SRV");

    D3D11_SAMPLER_DESC desktopSamplerDesc = {};
    desktopSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    desktopSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    desktopSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    desktopSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    hr = device->CreateSamplerState(&desktopSamplerDesc, desktopTexSampler.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create sampler state");

    D3D11_SAMPLER_DESC cursorSamplerDesc = {};
    cursorSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    cursorSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    cursorSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    cursorSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    cursorSamplerDesc.BorderColor[0] = 0;
    cursorSamplerDesc.BorderColor[1] = 0;
    cursorSamplerDesc.BorderColor[2] = 0;
    cursorSamplerDesc.BorderColor[3] = 0;
    hr = device->CreateSamplerState(&cursorSamplerDesc, cursorTexSampler.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create sampler state");

    cursorTexSize = 128;
    recreateCursorTexture_();

    decoder = std::make_unique<DecoderFFmpeg>(clock);
    decoder->setOutputResolution(width, height);
    decoder->start();

    flagRunRender.store(true, std::memory_order_release);
    renderThread = std::thread([this]() { renderLoop_(); });
}

void StreamViewerD3D::recreateCursorTexture_() {
    HRESULT hr;

    cursorTex.release();
    cursorSRV.release();

    D3D11_TEXTURE2D_DESC cursorTexDesc = {};
    cursorTexDesc.Width = cursorTexSize;
    cursorTexDesc.Height = cursorTexSize;
    cursorTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    cursorTexDesc.ArraySize = 1;
    cursorTexDesc.MipLevels = 1;
    cursorTexDesc.SampleDesc.Count = 1;
    cursorTexDesc.Usage = D3D11_USAGE_DYNAMIC;
    cursorTexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cursorTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = device->CreateTexture2D(&cursorTexDesc, nullptr, cursorTex.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to allocate cursor texture");

    D3D11_SHADER_RESOURCE_VIEW_DESC cursorSrvDesc = {};
    cursorSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    cursorSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    cursorSrvDesc.Texture2D.MipLevels = 1;
    cursorSrvDesc.Texture2D.MostDetailedMip = 0;
    hr = device->CreateShaderResourceView(cursorTex.ptr(), &cursorSrvDesc, cursorSRV.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create cursor SRV");
}

void StreamViewerD3D::renderLoop_() {
    HRESULT hr;
    bool desktopLoaded = false;
    bool cursorLoaded = false;
    bool usingXORCursor = false;
    float clearColor[4] = {0, 0, 0, 1};

    const uintptr_t nullval = 0;

    std::chrono::steady_clock::time_point lastStatPrint = std::chrono::steady_clock::now();
    StatisticMixer totalTime(300);
    StatisticMixer encodingTime(300);
    StatisticMixer networkTime(300);
    StatisticMixer decodingTime(300);

    D3D11Texture2D framebuffer;
    hr = swapChain->GetBuffer(0, framebuffer.guid(), framebuffer.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to get framebuffer");

    D3D11RenderTargetView framebufferRTV;
    hr = device->CreateRenderTargetView(framebuffer.ptr(), nullptr, framebufferRTV.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create framebuffer RTV");

    while (flagRunRender.load(std::memory_order_acquire)) {
        DesktopFrame<TextureSoftware> frame;
        if (!decoder->readFrame(&frame))
            continue;

        frame.timePresented = clock.time();
        if (frame.timeCaptured.count() != -1) {
            encodingTime.pushValue((frame.timeEncoded - frame.timeCaptured).count() / 1000.0f);
            totalTime.pushValue((frame.timePresented - frame.timeCaptured).count() / 1000.0f);
        }
        networkTime.pushValue((frame.timeReceived - frame.timeEncoded).count() / 1000.0f);
        decodingTime.pushValue((frame.timeDecoded - frame.timeReceived).count() / 1000.0f);

        if (std::chrono::steady_clock::now() - lastStatPrint >= std::chrono::milliseconds(5000)) {
            lastStatPrint = std::chrono::steady_clock::now();
            auto totStat = totalTime.calcStat();
            auto encStat = encodingTime.calcStat();
            auto netStat = networkTime.calcStat();
            auto decStat = decodingTime.calcStat();

            if (encStat.valid() && netStat.valid() && decStat.valid()) {
                log.info("Total latency: {:.2f}ms  (Encoding: {:.2f} ms", totStat.avg, encStat.avg);
                log.info("    Network: {:.2f} ms  Decoding: {:.2f} ms)", netStat.avg, decStat.avg);
            }
        }

        if (!frame.desktop.isEmpty()) {
            desktopLoaded = true;

            D3D11_MAPPED_SUBRESOURCE mapInfo;
            context->Map(desktopTex.ptr(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapInfo);

            uint8_t *dstPtr = reinterpret_cast<uint8_t *>(mapInfo.pData);
            uint8_t *srcPtr = frame.desktop.data[0];

            if (mapInfo.RowPitch != frame.desktop.linesize[0]) {
                for (int i = 0; i < height; i++)
                    memcpy(dstPtr + (i * mapInfo.RowPitch), srcPtr + (i * frame.desktop.linesize[0]), width * 4);
            } else {
                // Fast path
                memcpy(dstPtr, srcPtr, height * mapInfo.RowPitch);
            }

            context->Unmap(desktopTex.ptr(), 0);
        }

        if (frame.cursorShape) {
            cursorLoaded = true;

            auto &shape = *frame.cursorShape;
            log.assert_quit(shape.format == CursorShapeFormat::RGBA || shape.format == CursorShapeFormat::RGBA_XOR,
                            "Unexpected cursor shape format: {}", (int)shape.format);
            usingXORCursor = shape.format == CursorShapeFormat::RGBA_XOR;

            if (cursorTexSize < shape.width || cursorTexSize < shape.height) {
                cursorTexSize = std::max(shape.width, shape.height);
                recreateCursorTexture_();
            }

            uint8_t *pSrc = shape.image.data();

            D3D11_MAPPED_SUBRESOURCE mapInfo = {};
            hr = context->Map(cursorTex.ptr(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapInfo);
            log.assert_quit(SUCCEEDED(hr), "Failed to map cursor texture");

            uint8_t *pDst = reinterpret_cast<uint8_t *>(mapInfo.pData);
            memset(pDst, 0, cursorTexSize * mapInfo.RowPitch);
            for (int i = 0; i < shape.height; i++)
                memcpy(pDst + (i * mapInfo.RowPitch), pSrc + (shape.width * 4) * i, shape.width * 4);

            context->Unmap(cursorTex.ptr(), 0);
        }

        D3D11_MAPPED_SUBRESOURCE mapInfo = {};
        hr = context->Map(cbuffer.ptr(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapInfo);
        log.assert_quit(SUCCEEDED(hr), "Failed to map cbuffer");

        CursorBox_cbuffer *p = reinterpret_cast<CursorBox_cbuffer *>(mapInfo.pData);
        p->flagCursorVisible = false;
        p->flagCursorXOR = usingXORCursor;

        if (frame.cursorPos) {
            p->flagCursorVisible = !!(frame.cursorPos->visible && cursorLoaded);

            if (frame.cursorPos->visible) {
                int cursorX = frame.cursorPos->xScaler.imul(frame.cursorPos->x);
                int cursorY = frame.cursorPos->yScaler.imul(frame.cursorPos->y);

                p->cursorPos[0] = (float)cursorX / width;
                p->cursorPos[1] = (float)cursorY / height;
                p->cursorSize[0] = (float)cursorTexSize / width * frame.cursorPos->xScaler.toFloat();
                p->cursorSize[1] = (float)cursorTexSize / height * frame.cursorPos->yScaler.toFloat();
            } else {
                p->cursorPos[0] = 0;
                p->cursorPos[1] = 0;
                p->cursorSize[0] = 0;
                p->cursorSize[1] = 0;
            }
        }

        context->Unmap(cbuffer.ptr(), 0);

        context->ClearRenderTargetView(framebufferRTV.ptr(), clearColor);
        context->OMSetRenderTargets(1, framebufferRTV.data(), nullptr);

        if (desktopLoaded) {
            DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
            swapChain->GetDesc1(&swapChainDesc);

            D3D11_VIEWPORT viewport = {0, 0, (float)(swapChainDesc.Width), (float)(swapChainDesc.Height), 0, 1};
            context->RSSetViewports(1, &viewport);

            context->VSSetShader(vertexShaderFullscreen.ptr(), nullptr, 0);
            context->VSSetConstantBuffers(0, 1, cbuffer.data());

            context->PSSetShader(pixelShaderDesktop.ptr(), nullptr, 0);
            context->PSSetConstantBuffers(0, 1, cbuffer.data());
            context->PSSetShaderResources(0, 1, desktopSRV.data());
            context->PSSetShaderResources(1, 1, cursorSRV.data());
            context->PSSetSamplers(0, 1, desktopTexSampler.data());
            context->PSSetSamplers(1, 1, cursorTexSampler.data());

            context->IASetInputLayout(nullptr);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->IASetVertexBuffers(0, 1, (ID3D11Buffer **)&nullval, (UINT *)&nullval, (UINT *)&nullval);
            context->Draw(3, 0);
        }

        hr = swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
        log.assert_quit(SUCCEEDED(hr), "Failed to present next frame");
    }
}
