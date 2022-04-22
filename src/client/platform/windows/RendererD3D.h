#ifndef TWILIGHT_CLIENT_PLATFORM_WINDOWS_RENDERERD3D_H
#define TWILIGHT_CLIENT_PLATFORM_WINDOWS_RENDERERD3D_H

#include "common/log.h"
#include "common/DesktopFrame.h"

#include "common/platform/windows/DxgiHelper.h"

class RendererD3D {
public:
    RendererD3D();
    RendererD3D(const RendererD3D& copy) = delete;
    RendererD3D(RendererD3D&& move);
    ~RendererD3D();

    void init(HWND hWnd, DxgiHelper dxgiHelper, D3D11Device device);

    void setCaptureResolution(int w, int h);
    void resized(int w, int h);

    void clear();
    void render(const DesktopFrame<D3D11Texture2D>& frame);

private:
    void recreateCursorTexture_();

    static NamedLogger log;

    HWND hWnd;
    int captureWidth, captureHeight;
    int renderWidth, renderHeight;
    int cursorTexSize;
    UINT swapChainFlags;

    bool hasCursor;
    bool usingXORCursor;

    DxgiHelper dxgiHelper;
    D3D11Device device;
    D3D11DeviceContext context;
    DxgiSwapChain1 swapChain;

    D3D11Buffer cbuffer;
    D3D11SamplerState desktopTexSampler;
    D3D11Texture2D cursorTex;
    D3D11SamplerState cursorTexSampler;
    D3D11ShaderResourceView cursorSRV;
    D3D11VertexShader vsFullscreen;
    D3D11PixelShader psDesktop;
};

#endif
