#ifndef TWILIGHT_CLIENT_PLATFORM_WINDOWS_STREAMVIEWERD3D_H
#define TWILIGHT_CLIENT_PLATFORM_WINDOWS_STREAMVIEWERD3D_H

#include <thread>

#include <packet.pb.h>

#include "common/ByteBuffer.h"
#include "common/log.h"
#include "common/util.h"

#include "common/platform/windows/ComWrapper.h"
#include "common/platform/windows/DxgiHelper.h"

#include "client/NetworkClock.h"
#include "client/StreamViewerBase.h"

#include "client/platform/software/DecoderOpenH264.h"
#include "client/platform/software/DecoderFFmpeg.h"

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
    void init_();
    void recreateCursorTexture_();
    void renderLoop_();

    LoggerPtr log;
    NetworkClock &clock;

    std::atomic<bool> flagInitialized;
    std::atomic<bool> flagRunRender;

    int width, height;
    int cursorTexSize;

    std::thread renderThread;

    std::unique_ptr<DecoderFFmpeg> decoder;
    std::shared_ptr<CursorShape> pendingCursorChange;

    DxgiHelper dxgiHelper;
    D3D11Device device;
    D3D11DeviceContext context;
    DxgiSwapChain1 swapChain;
    D3D11Texture2D desktopTex;
    D3D11VertexShader vertexShaderFullscreen;
    D3D11PixelShader pixelShaderDesktop;
    D3D11Buffer cbuffer;
    D3D11ShaderResourceView desktopSRV;
    D3D11SamplerState desktopTexSampler;
    D3D11SamplerState cursorTexSampler;

    D3D11Texture2D cursorTex;
    D3D11ShaderResourceView cursorSRV;
};

#endif
