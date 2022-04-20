#include "StreamServer.h"

#include "server/CapturePipelineFactory.h"

#include <mbedtls/sha256.h>

TWILIGHT_DEFINE_LOGGER(StreamServer);

constexpr uint16_t SERVICE_PORT = 6495;
constexpr int32_t PROTOCOL_VERSION = 1;

StreamServer::StreamServer() : requestedWidth(0), requestedHeight(0), flagRunDeleter(true), streaming(false) {
    knownClients.loadFile("clients.toml");

    deleterThread = std::thread([this]() {
        std::unique_lock lock(connectionsLock);
        while (flagRunDeleter.load(std::memory_order_relaxed)) {
            if (deleteReq.empty()) {
                deleteReqCV.wait(lock);
                continue;
            }

            Connection* conn = deleteReq.front();
            deleteReq.pop_front();

            while (true) {
                bool erased = false;
                for (auto it = connections.begin(); it != connections.end(); ++it) {
                    if (it->get() == conn) {
                        connections.erase(it);
                        erased = true;
                        break;
                    }
                }
                if (!erased)
                    break;
            }

            if (connections.empty() && streaming) {
                streaming = false;
                audioEncoder.stop();
                capture->stop();
            }
        }
    });

    auto factory = CapturePipelineFactory::createInstance();
    auto opt = factory->getBestOption();
    capture = factory->createPipeline(clock, opt.first, opt.second);

    capture->setOutputCallback([this](DesktopFrame<ByteBuffer>&& cap) { processOutput_(std::move(cap)); });
    audioEncoder.setOnAudioData([this](const uint8_t* data, size_t len) {
        msg::Packet pkt;
        pkt.set_extra_data_len(len);
        auto audioFrame = pkt.mutable_audio_frame();
        audioFrame->set_channels(2);
        broadcast_(pkt, data);
    });

    server.setOnNewConnection([this](std::unique_ptr<NetworkSocket>&& newSock) {
        std::lock_guard lock(connectionsLock);
        connections.push_back(std::make_unique<Connection>(this, std::move(newSock)));
    });
}

StreamServer::~StreamServer() {
    flagRunDeleter.store(false, std::memory_order_release);
    deleteReqCV.notify_all();
    deleterThread.join();
}

void StreamServer::start() {
    server.startListen(SERVICE_PORT);
}

void StreamServer::stop() {
    server.stopListen();

    std::lock_guard lock(connectionsLock);
    for (std::unique_ptr<Connection>& conn : connections)
        conn->disconnect();

    // Drop all elements
    connections.resize(0);
}

void StreamServer::getNativeMode(int* w, int* h, Rational* fps) {
    log.assert_quit(capture->init(), "Failed to initialize CapturePipeline");
    capture->getNativeMode(w, h, fps);
}

void StreamServer::getCaptureResolution(int* w, int* h) {
    Rational fps;
    capture->getNativeMode(w, h, &fps);
}

void StreamServer::getVideoResolution(int* w, int* h) {
    *w = requestedWidth;
    *h = requestedHeight;
}

void StreamServer::onDisconnected(Connection* conn) {
    std::lock_guard lock(connectionsLock);

    deleteReq.push_back(conn);
    deleteReqCV.notify_one();
}

void StreamServer::configureStream(Connection* conn, int width, int height, Rational framerate) {
    requestedWidth = width;
    requestedHeight = height;
    requestedFramerate = framerate;
}

bool StreamServer::startStream(Connection* conn) {
    if (streaming)
        return false;

    // TODO: Allow multiple clients
    streaming = true;
    capture->setEncoderMode(requestedWidth, requestedHeight, requestedFramerate);
    capture->start();
    audioEncoder.start();
    return true;
}

void StreamServer::endStream(Connection* conn) {
    if (streaming) {
        streaming = false;
        audioEncoder.stop();
        capture->stop();
    }
}

ByteBuffer StreamServer::getLocalCert() {
    return server.getCert().der();
}

void StreamServer::processOutput_(DesktopFrame<ByteBuffer>&& cap) {
    if (cap.cursorPos)
        cursorPos = std::move(cap.cursorPos);

    msg::Packet pkt;
    if (cap.cursorShape) {
        msg::CursorShape* m = pkt.mutable_cursor_shape();
        m->set_width(cap.cursorShape->width);
        m->set_height(cap.cursorShape->height);
        m->set_hotspot_x(cap.cursorShape->hotspotX);
        m->set_hotspot_y(cap.cursorShape->hotspotY);
        switch (cap.cursorShape->format) {
        case CursorShapeFormat::RGBA:
            m->set_format(msg::CursorShape_Format_RGBA);
            break;
        case CursorShapeFormat::RGBA_XOR:
            m->set_format(msg::CursorShape_Format_RGBA_XOR);
            break;
        default:
            log.error_quit("Unknown cursor shape format: {}", (int)cap.cursorShape->format);
        }

        pkt.set_extra_data_len(cap.cursorShape->image.size());
        broadcast_(pkt, cap.cursorShape->image.data());
    }

    msg::DesktopFrame* m = pkt.mutable_desktop_frame();
    if (cursorPos) {
        m->set_cursor_visible(cursorPos->visible);
        if (cursorPos->visible) {
            m->set_cursor_x(cursorPos->x);
            m->set_cursor_y(cursorPos->y);
        }
    } else {
        m->set_cursor_visible(false);
    }

    m->set_time_captured(cap.timeCaptured.count());
    m->set_time_encoded(cap.timeEncoded.count());

    m->set_is_idr(cap.isIDR);

    pkt.set_extra_data_len(cap.desktop.size());
    broadcast_(pkt, cap.desktop);
}

void StreamServer::broadcast_(const msg::Packet& pkt, const uint8_t* extraData) {
    for (const std::unique_ptr<Connection>& conn : connections)
        conn->send(pkt, extraData);
}

void StreamServer::broadcast_(const msg::Packet& pkt, const ByteBuffer& extraData) {
    for (const std::unique_ptr<Connection>& conn : connections)
        conn->send(pkt, extraData);
}
