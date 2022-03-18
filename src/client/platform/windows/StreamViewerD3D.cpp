#include "StreamViewerD3D.h"

#include <vector>

#include "common/StatisticMixer.h"
#include "common/util.h"

static const float quadVertex[] = {-1, -1, -1, 1, 1, -1, 1, 1};
static const UINT quadVertexStride = 2 * sizeof(quadVertex[0]);
static const UINT quadVertexOffset = 0;
static const UINT quadVertexCount = 4;

StreamViewerD3D::StreamViewerD3D(NetworkClock &clock)
    : StreamViewerBase(), log(createNamedLogger("StreamViewerD3D")), clock(clock) {
    width = 1920;
    height = 1080;
}

StreamViewerD3D::~StreamViewerD3D() {
    log->info("Stopping!!!");

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
        _init();
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

    now.cursorPos = std::make_shared<CursorPos>();
    now.cursorPos->visible = res.cursor_visible();
    if (now.cursorPos->visible) {
        now.cursorPos->x = res.cursor_x();
        now.cursorPos->y = res.cursor_y();
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
    now->height = data.height();
    now->width = data.width();
    now->hotspotX = data.hotspot_x();
    now->hotspotY = data.hotspot_y();
    now->image.write(0, extraData, pkt.extra_data_len());

    std::atomic_exchange(&pendingCursorChange, now);
}

void StreamViewerD3D::_init() {
    HRESULT hr;

    hr = CreateDXGIFactory1(dxgiFactory.guid(), dxgiFactory.data());
    check_quit(FAILED(hr), log, "Failed to create dxgi factory");

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                           D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_BGRA_SUPPORT, &featureLevel, 1,
                           D3D11_SDK_VERSION, device.data(), nullptr, context.data());
    check_quit(FAILED(hr), log, "Failed to create D3D11 device");

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = 0;
    swapChainDesc.Height = 0;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    hr = dxgiFactory->CreateSwapChainForHwnd(device.ptr(), hwnd(), &swapChainDesc, nullptr, nullptr, swapChain.data());
    check_quit(FAILED(hr), log, "Failed to create swap chain");

    D3D11Texture2D framebuffer;
    hr = swapChain->GetBuffer(0, framebuffer.guid(), framebuffer.data());
    check_quit(FAILED(hr), log, "Failed to get framebuffer");

    hr = device->CreateRenderTargetView(framebuffer.ptr(), nullptr, framebufferRTV.data());
    check_quit(FAILED(hr), log, "Failed to create framebuffer RTV");

    // FIXME: Unchecked std::optional unwrapping
    ByteBuffer vertexBlobFull = loadEntireFile("viewer-vs_full.fxc").value();
    hr = device->CreateVertexShader(vertexBlobFull.data(), vertexBlobFull.size(), nullptr, vertexShaderFull.data());
    check_quit(FAILED(hr), log, "Failed to create vertex shader (full)");

    ByteBuffer vertexBlobBox = loadEntireFile("viewer-vs_box.fxc").value();
    hr = device->CreateVertexShader(vertexBlobBox.data(), vertexBlobBox.size(), nullptr, vertexShaderBox.data());
    check_quit(FAILED(hr), log, "Failed to create vertex shader (box)");

    ByteBuffer pixelBlob = loadEntireFile("viewer-ps_main.fxc").value();
    hr = device->CreatePixelShader(pixelBlob.data(), pixelBlob.size(), nullptr, pixelShader.data());
    check_quit(FAILED(hr), log, "Failed to create pixel shader");

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = -FLT_MAX;
    samplerDesc.MaxLOD = FLT_MAX;
    device->CreateSamplerState(&samplerDesc, clampSampler.data());

    D3D11_INPUT_ELEMENT_DESC inputLayoutDesc[] = {
        {"POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    device->CreateInputLayout(inputLayoutDesc, 1, vertexBlobFull.data(), vertexBlobFull.size(), inputLayoutFull.data());
    device->CreateInputLayout(inputLayoutDesc, 1, vertexBlobBox.data(), vertexBlobBox.size(), inputLayoutBox.data());

    D3D11_BUFFER_DESC vertexBufferDesc = {};
    vertexBufferDesc.ByteWidth = sizeof(quadVertex);
    vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vertexBufferData = {};
    vertexBufferData.pSysMem = quadVertex;
    device->CreateBuffer(&vertexBufferDesc, &vertexBufferData, vertexBuffer.data());

    D3D11_BUFFER_DESC cbufferDesc = {};
    cbufferDesc.ByteWidth = sizeof(float) * 4;
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
    check_quit(FAILED(hr), log, "Failed to allocate desktop texture");

    D3D11_SHADER_RESOURCE_VIEW_DESC desktopSrvDesc = {};
    desktopSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    desktopSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desktopSrvDesc.Texture2D.MipLevels = 1;
    desktopSrvDesc.Texture2D.MostDetailedMip = 0;
    device->CreateShaderResourceView(desktopTex.ptr(), &desktopSrvDesc, desktopSRV.data());
    check_quit(FAILED(hr), log, "Failed to create desktop SRV");

    D3D11_BLEND_DESC blendStateDesc = {};
    blendStateDesc.RenderTarget[0].BlendEnable = true;
    blendStateDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendStateDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendStateDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendStateDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendStateDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendStateDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendStateDesc.RenderTarget[0].RenderTargetWriteMask = 0x0f;
    device->CreateBlendState(&blendStateDesc, blendState.data());

    cursorTexWidth = 128;
    cursorTexHeight = 128;
    _recreateCursorTexture();

    decoder = std::make_unique<DecoderSoftware>(clock);
    decoder->start();

    flagRunRender.store(true, std::memory_order_release);
    renderThread = std::thread([this]() { _renderLoop(); });
}

void StreamViewerD3D::_recreateCursorTexture() {
    HRESULT hr;

    cursorTex.release();
    cursorSRV.release();

    D3D11_TEXTURE2D_DESC cursorTexDesc = {};
    cursorTexDesc.Width = cursorTexWidth;
    cursorTexDesc.Height = cursorTexHeight;
    cursorTexDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    cursorTexDesc.ArraySize = 1;
    cursorTexDesc.MipLevels = 1;
    cursorTexDesc.SampleDesc.Count = 1;
    cursorTexDesc.Usage = D3D11_USAGE_DYNAMIC;
    cursorTexDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cursorTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = device->CreateTexture2D(&cursorTexDesc, nullptr, cursorTex.data());
    check_quit(FAILED(hr), log, "Failed to allocate cursor texture");

    D3D11_SHADER_RESOURCE_VIEW_DESC cursorSrvDesc = {};
    cursorSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    cursorSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    cursorSrvDesc.Texture2D.MipLevels = 1;
    cursorSrvDesc.Texture2D.MostDetailedMip = 0;
    hr = device->CreateShaderResourceView(cursorTex.ptr(), &cursorSrvDesc, cursorSRV.data());
    check_quit(FAILED(hr), log, "Failed to create cursor SRV");
}

void StreamViewerD3D::_renderLoop() {
    bool desktopLoaded = false;
    bool cursorLoaded = false;
    float clearColor[4] = {0, 0, 0, 1};

    bool cursorVisible = false;
    int cursorX = -1, cursorY = -1;

    std::chrono::steady_clock::time_point lastStatPrint = std::chrono::steady_clock::now();
    StatisticMixer totalTime(480);
    StatisticMixer encodingTime(480);
    StatisticMixer networkTime(480);
    StatisticMixer decodingTime(480);

    while (flagRunRender.load(std::memory_order_acquire)) {
        DesktopFrame<TextureSoftware> frame = decoder->popData();

        if (frame.desktop.isEmpty())
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
                log->info("Total latency: {:.2f}ms  (Encoding: {:.2f} ms  Network: {:.2f} ms  Decoding: {:.2f} ms)",
                          totStat.avg, encStat.avg, netStat.avg, decStat.avg);
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

        if (frame.cursorPos) {
            cursorVisible = frame.cursorPos->visible;

            if (cursorVisible && (frame.cursorPos->x != cursorX || frame.cursorPos->y != cursorY)) {
                cursorX = frame.cursorPos->x;
                cursorY = frame.cursorPos->y;

                D3D11_MAPPED_SUBRESOURCE mapInfo = {};
                context->Map(cbuffer.ptr(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapInfo);
                float *pData = reinterpret_cast<float *>(mapInfo.pData);
                pData[0] = (float)cursorX / width;
                pData[1] = (float)cursorY / height;
                pData[2] = (float)cursorTexWidth / width;
                pData[3] = (float)cursorTexHeight / height;
                context->Unmap(cbuffer.ptr(), 0);
            }
        }

        if (frame.cursorShape) {
            cursorLoaded = true;
            auto &shape = *frame.cursorShape;

            if (cursorTexWidth < shape.width || cursorTexHeight < shape.height) {
                cursorTexWidth = cursorTexHeight = std::max(shape.width, shape.height);
                _recreateCursorTexture();
            }

            D3D11_MAPPED_SUBRESOURCE mapInfo = {};
            context->Map(cursorTex.ptr(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapInfo);
            uint8_t *pDst = reinterpret_cast<uint8_t *>(mapInfo.pData);
            uint8_t *pSrc = shape.image.data();
            memset(pDst, 0, cursorTexHeight * mapInfo.RowPitch);
            for (int i = 0; i < shape.height; i++)
                memcpy(pDst + (i * mapInfo.RowPitch), pSrc + (shape.width * 4) * i, shape.width * 4);
            context->Unmap(cursorTex.ptr(), 0);
        }

        context->ClearRenderTargetView(framebufferRTV.ptr(), clearColor);
        context->OMSetRenderTargets(1, framebufferRTV.data(), nullptr);
        context->OMSetBlendState(blendState.ptr(), nullptr, 0xffffffff);

        if (desktopLoaded) {
            DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
            swapChain->GetDesc1(&swapChainDesc);

            D3D11_VIEWPORT viewport = {0, 0, (float)(swapChainDesc.Width), (float)(swapChainDesc.Height), 0, 1};
            context->RSSetViewports(1, &viewport);

            context->VSSetShader(vertexShaderFull.ptr(), nullptr, 0);

            context->PSSetShader(pixelShader.ptr(), nullptr, 0);
            context->PSSetShaderResources(0, 1, desktopSRV.data());
            context->PSSetSamplers(0, 1, clampSampler.data());

            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            context->IASetInputLayout(inputLayoutFull.ptr());
            context->IASetVertexBuffers(0, 1, vertexBuffer.data(), &quadVertexStride, &quadVertexOffset);
            context->Draw(quadVertexCount, 0);

            if (cursorLoaded && cursorVisible) {
                context->VSSetShader(vertexShaderBox.ptr(), nullptr, 0);
                context->VSSetConstantBuffers(0, 1, cbuffer.data());

                context->PSSetShaderResources(0, 1, cursorSRV.data());

                context->IASetInputLayout(inputLayoutBox.ptr());
                context->IASetVertexBuffers(0, 1, vertexBuffer.data(), &quadVertexStride, &quadVertexOffset);
                context->Draw(quadVertexCount, 0);
            }
        }

        swapChain->Present(1, 0);
    }
}