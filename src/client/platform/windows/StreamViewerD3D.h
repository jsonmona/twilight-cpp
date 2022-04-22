#ifndef TWILIGHT_CLIENT_PLATFORM_WINDOWS_STREAMVIEWERD3D_H
#define TWILIGHT_CLIENT_PLATFORM_WINDOWS_STREAMVIEWERD3D_H

#include "common/ByteBuffer.h"
#include "common/log.h"
#include "common/util.h"

#include "client/NetworkClock.h"
#include "client/StreamClient.h"
#include "client/StreamViewerBase.h"

#include "client/platform/windows/RendererD3D.h"
#include "client/platform/windows/DecodePipelineSoftD3D.h"

#include <packet.pb.h>

#include <thread>

class StreamViewerD3D : public StreamViewerBase {
    Q_OBJECT;

public:
    StreamViewerD3D(std::shared_ptr<NetworkClock> clock, StreamClient *client);
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
    void renderLoop_();

    static NamedLogger log;

    std::shared_ptr<NetworkClock> clock;
    StreamClient *sc;

    HWND hWnd;
    int captureWidth, captureHeight;
    int pixWidth, pixHeight;

    std::atomic<bool> flagWindowReady;
    std::atomic<bool> flagStreamStarted;
    std::atomic<bool> flagInitialized;
    std::atomic<bool> flagRunRender;

    std::thread renderThread;
    std::shared_ptr<CursorShape> pendingCursorChange;

    DecodePipelineSoftD3D pipeline;
};

#endif
