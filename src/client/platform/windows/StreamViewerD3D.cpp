#include "StreamViewerD3D.h"

#include "common/StatisticMixer.h"
#include "common/util.h"

#include "client/platform/software/DecoderFFmpeg.h"

#include <vector>

TWILIGHT_DEFINE_LOGGER(StreamViewerD3D);

StreamViewerD3D::StreamViewerD3D(std::shared_ptr<NetworkClock> clock_, StreamClient *client)
    : StreamViewerBase(),
      clock(std::move(clock_)),
      sc(client),
      flagWindowReady(false),
      flagStreamStarted(false),
      flagInitialized(false),
      flagRunRender(false),
      pipeline(std::make_unique<DecoderFFmpeg>()) {
    pipeline.getDecoder()->init(CodecType::VP8, clock);
}

StreamViewerD3D::~StreamViewerD3D() {
    bool wasInitialized = flagInitialized.load();

    if (wasInitialized) {
        flagRunRender.store(false, std::memory_order_release);
        pipeline.stop();
        renderThread.join();
    } else {
        log.assert_quit(!flagRunRender.load(std::memory_order_relaxed), "Renderer running when not initialized!");
    }
}

void StreamViewerD3D::resizeEvent(QResizeEvent *ev) {
    StreamViewerBase::resizeEvent(ev);

    hWnd = hwnd();
    RECT rect;
    log.assert_quit(GetClientRect(hWnd, &rect), "Failed to retrieve client rect");
    pixWidth = rect.right - rect.left;
    pixHeight = rect.bottom - rect.top;

    if (!flagWindowReady.exchange(true) && flagStreamStarted.load())
        init_();
}

void StreamViewerD3D::setDrawCursor(bool newval) {}

void StreamViewerD3D::processDesktopFrame(const msg::Packet &pkt, uint8_t *extraData) {
    if (!flagStreamStarted.exchange(true) && flagWindowReady.load())
        init_();

    auto &res = pkt.desktop_frame();
    clock->monotonicHint(res.time_encoded());

    DesktopFrame<ByteBuffer> now;
    now.desktop.write(0, extraData, pkt.extra_data_len());

    now.timeCaptured = std::chrono::microseconds(res.time_captured());
    now.timeEncoded = std::chrono::microseconds(res.time_encoded());
    now.timeReceived = clock->time();

    now.isIDR = res.is_idr();

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

    pipeline.pushData(std::move(now));
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
    bool prev = flagInitialized.exchange(true, std::memory_order_relaxed);
    if (prev)
        return;

    int videoWidth, videoHeight;
    sc->getVideoResolution(&videoWidth, &videoHeight);
    sc->getCaptureResolution(&captureWidth, &captureHeight);

    pipeline.setInputResolution(videoWidth, videoHeight);
    pipeline.setOutputResolution(pixWidth, pixHeight);
    pipeline.start();
    flagRunRender.store(true, std::memory_order_release);
    renderThread = std::thread([this]() { renderLoop_(); });
}

void StreamViewerD3D::renderLoop_() {
    std::chrono::steady_clock::time_point lastStatPrint = std::chrono::steady_clock::now();
    StatisticMixer totalTime(300);
    StatisticMixer encodingTime(300);
    StatisticMixer networkTime(300);
    StatisticMixer decodingTime(300);

    RendererD3D renderer;
    renderer.setCaptureResolution(captureWidth, captureHeight);
    renderer.init(hwnd(), pipeline.getDxgiHelper(), pipeline.getDevice());
    renderer.resized(pixWidth, pixHeight);

    renderer.clear();

    while (flagRunRender.load(std::memory_order_acquire)) {
        // TODO: Handle window size changes

        DesktopFrame<D3D11Texture2D> frame;
        if (!pipeline.render(&renderer, &frame))
            continue;

        frame.timePresented = clock->time();
        if (frame.timeCaptured.count() > 0) {
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
    }
}
