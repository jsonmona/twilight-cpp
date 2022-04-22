#include "RendererD3D.h"

#include "hlsl-viewer.h"

#include "common/util.h"

TWILIGHT_DEFINE_LOGGER(RendererD3D);

struct cbufferType {
    float cursorPos[2];
    float cursorSize[2];
    uint32_t flagCursorVisible;
    uint32_t flagCursorXOR;
    uint32_t dummy_[2];
};
static_assert(sizeof(cbufferType) % 16 == 0, "cbuffer size must be multiple of 16");

RendererD3D::RendererD3D()
    : hWnd(0),
      captureWidth(-1),
      captureHeight(-1),
      renderWidth(-1),
      renderHeight(-1),
      swapChainFlags(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING),
      hasCursor(false),
      usingXORCursor(false) {}

RendererD3D::RendererD3D(RendererD3D&& move) = default;

RendererD3D::~RendererD3D() {}

void RendererD3D::init(HWND hWnd_, DxgiHelper dxgiHelper_, D3D11Device device_) {
    log.assert_quit(0 < captureWidth && 0 < captureHeight, "Capture resolution not set before init!");

    context.release();
    swapChain.release();
    cbuffer.release();
    desktopTexSampler.release();
    cursorTex.release();
    cursorTexSampler.release();
    cursorSRV.release();
    vsFullscreen.release();
    psDesktop.release();

    hasCursor = false;
    hWnd = hWnd_;

    dxgiHelper = std::move(dxgiHelper_);
    device = std::move(device_);
    device->GetImmediateContext(context.data());

    context->ClearState();
    context->Flush();

    HRESULT hr;

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = 0;
    swapChainDesc.Height = 0;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = false;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Scaling = DXGI_SCALING_NONE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swapChainDesc.Flags = swapChainFlags;
    hr = dxgiHelper.getFactory()->CreateSwapChainForHwnd(device.ptr(), hWnd, &swapChainDesc, nullptr, nullptr,
                                                         swapChain.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create swap chain for hwnd");

    D3D11_BUFFER_DESC cbufferDesc = {};
    cbufferDesc.ByteWidth = sizeof(cbufferType);
    cbufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&cbufferDesc, nullptr, cbuffer.data());

    D3D11_SAMPLER_DESC desktopSamplerDesc = {};
    desktopSamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    desktopSamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    desktopSamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    desktopSamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    hr = device->CreateSamplerState(&desktopSamplerDesc, desktopTexSampler.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create sampler state");

    cursorTexSize = 128;
    recreateCursorTexture_();

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

    hr = device->CreateVertexShader(TWILIGHT_ARRAY_WITHLEN(g_vs_fullscreen), nullptr, vsFullscreen.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create vertex shader");

    hr = device->CreatePixelShader(TWILIGHT_ARRAY_WITHLEN(g_ps_desktop), nullptr, psDesktop.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create pixel shader");
}

void RendererD3D::setCaptureResolution(int w, int h) {
    captureWidth = w;
    captureHeight = h;
}

void RendererD3D::resized(int w, int h) {
    renderWidth = w;
    renderHeight = h;
    swapChain->ResizeBuffers(2, renderWidth, renderHeight, DXGI_FORMAT_R8G8B8A8_UNORM, swapChainFlags);
}

void RendererD3D::clear() {
    static const float clearColor[4] = {0, 0, 0, 0};
    HRESULT hr;

    D3D11Texture2D framebuffer;
    hr = swapChain->GetBuffer(0, framebuffer.guid(), framebuffer.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to get framebuffer");

    D3D11RenderTargetView framebufferRTV;
    hr = device->CreateRenderTargetView(framebuffer.ptr(), nullptr, framebufferRTV.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create framebuffer RTV");

    context->ClearRenderTargetView(framebufferRTV.ptr(), clearColor);
}

void RendererD3D::render(const DesktopFrame<D3D11Texture2D>& frame) {
    static const float clearColor[4] = {0, 0, 0, 0};
    static void* const nullval = nullptr;

    HRESULT hr;

    if (frame.cursorShape) {
        CursorShape* shape = frame.cursorShape.get();
        if (cursorTexSize < shape->width || cursorTexSize < shape->height) {
            cursorTexSize = std::max(shape->width, shape->height);
            recreateCursorTexture_();
        }

        D3D11_MAPPED_SUBRESOURCE mapInfo;
        hr = context->Map(cursorTex.ptr(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapInfo);
        log.assert_quit(SUCCEEDED(hr), "Failed to map cursor texture for writing");

        memset(mapInfo.pData, 0, mapInfo.RowPitch * cursorTexSize);
        uint8_t* src = shape->image.data();
        uint8_t* dst = reinterpret_cast<uint8_t*>(mapInfo.pData);
        for (int i = 0; i < shape->height; i++) {
            memcpy(dst, src, shape->width * 4);
            dst += mapInfo.RowPitch;
            src += shape->width * 4;
        }

        context->Unmap(cursorTex.ptr(), 0);
        hasCursor = true;
        usingXORCursor = shape->format == CursorShapeFormat::RGBA_XOR;
    }

    /* Update cbuffer */ {
        cbufferType data = {};
        if (frame.cursorPos != nullptr) {
            CursorPos* pos = frame.cursorPos.get();
            data.flagCursorVisible = hasCursor && pos->visible;
            data.flagCursorXOR = usingXORCursor;
            if (data.flagCursorVisible) {
                data.cursorPos[0] = (float)frame.cursorPos->x / captureWidth;
                data.cursorPos[1] = (float)frame.cursorPos->y / captureHeight;
                data.cursorSize[0] = (float)cursorTexSize / captureWidth;
                data.cursorSize[1] = (float)cursorTexSize / captureHeight;
            }
        } else {
            data.flagCursorVisible = false;
            data.cursorSize[0] = 1;
            data.cursorSize[1] = 1;
        }

        D3D11_MAPPED_SUBRESOURCE mapInfo;
        hr = context->Map(cbuffer.ptr(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapInfo);
        log.assert_quit(SUCCEEDED(hr), "Failed to map cbuffer for writing");

        memcpy(mapInfo.pData, &data, sizeof(data));

        context->Unmap(cbuffer.ptr(), 0);
    }

    D3D11Texture2D framebuffer;
    hr = swapChain->GetBuffer(0, framebuffer.guid(), framebuffer.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to get framebuffer");

    D3D11RenderTargetView framebufferRTV;
    hr = device->CreateRenderTargetView(framebuffer.ptr(), nullptr, framebufferRTV.data());
    log.assert_quit(SUCCEEDED(hr), "Failed to create framebuffer RTV");

    context->ClearRenderTargetView(framebufferRTV.ptr(), clearColor);
    context->OMSetRenderTargets(1, framebufferRTV.data(), nullptr);

    if (frame.desktop.isValid()) {
        D3D11ShaderResourceView desktopSRV;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        device->CreateShaderResourceView(frame.desktop.ptr(), &srvDesc, desktopSRV.data());
        log.assert_quit(SUCCEEDED(hr), "Failed to create desktop SRV");

        D3D11_VIEWPORT viewport = {0, 0, (float)renderWidth, (float)renderHeight, 0, 1};
        context->RSSetViewports(1, &viewport);

        context->VSSetShader(vsFullscreen.ptr(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, cbuffer.data());

        context->PSSetShader(psDesktop.ptr(), nullptr, 0);
        context->PSSetConstantBuffers(0, 1, cbuffer.data());
        context->PSSetShaderResources(0, 1, desktopSRV.data());
        context->PSSetShaderResources(1, 1, cursorSRV.data());
        context->PSSetSamplers(0, 1, desktopTexSampler.data());
        context->PSSetSamplers(1, 1, cursorTexSampler.data());

        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetVertexBuffers(0, 1, (ID3D11Buffer**)&nullval, (UINT*)&nullval, (UINT*)&nullval);
        context->Draw(3, 0);
    }

    context->ClearState();

    hr = swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
    log.assert_quit(SUCCEEDED(hr), "Failed to present swapchain");
}

void RendererD3D::recreateCursorTexture_() {
    HRESULT hr;

    cursorTex.release();
    cursorSRV.release();
    hasCursor = false;

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
