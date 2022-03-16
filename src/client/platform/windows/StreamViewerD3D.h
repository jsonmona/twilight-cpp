#ifndef TWILIGHT_CLIENT_PLATFORM_WINDOWS_STREAMVIEWERD3D_H
#define TWILIGHT_CLIENT_PLATFORM_WINDOWS_STREAMVIEWERD3D_H

#include <thread>

#include <packet.pb.h>

#include "common/ByteBuffer.h"
#include "common/log.h"
#include "common/platform/windows/ComWrapper.h"
#include "common/util.h"

#include "client/NetworkClock.h"
#include "client/StreamViewerBase.h"

#include "client/platform/software/DecoderSoftware.h"

class StreamViewerD3D : public StreamViewerBase {
    Q_OBJECT;

public:
    explicit StreamViewerD3D(NetworkClock &clock);
    ~StreamViewerD3D() override;

    QPaintEngine *paintEngine() const override { return nullptr; }

protected:
    void setDrawCursor(bool newval) override;
    void processDesktopFrame(const msg::Packet &pkt, uint8_t *extraData) override;
    void processCursorShape(const msg::Packet &pkt, uint8_t *extraData) override;

    void resizeEvent(QResizeEvent *ev) override;

private:
    HWND hwnd() const { return reinterpret_cast<HWND>(winId()); }
    void _init();
    void _recreateCursorTexture();
    void _renderLoop();

    LoggerPtr log;

    std::atomic<bool> flagInitialized = false;
    std::atomic<bool> flagRunRender = false;

    std::thread renderThread;
    NetworkClock &clock;

    DxgiFactory5 dxgiFactory;
    D3D11Device device;
    D3D11DeviceContext context;
    ComWrapper<IDXGISwapChain1> swapChain;
    D3D11RenderTargetView framebufferRTV;
    D3D11Texture2D desktopTex;
    D3D11Texture2D cursorTex;
    D3D11VertexShader vertexShaderFull;
    D3D11VertexShader vertexShaderBox;
    D3D11PixelShader pixelShader;
    D3D11SamplerState clampSampler;
    D3D11InputLayout inputLayoutFull;
    D3D11InputLayout inputLayoutBox;
    D3D11Buffer vertexBuffer;
    D3D11Buffer cbuffer;
    D3D11ShaderResourceView desktopSRV;
    D3D11ShaderResourceView cursorSRV;
    D3D11BlendState blendState;

    std::unique_ptr<DecoderSoftware> decoder;
    std::shared_ptr<CursorShape> pendingCursorChange;

    int width, height;
    int cursorTexWidth, cursorTexHeight;
};

#endif