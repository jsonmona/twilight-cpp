#ifndef TWILIGHT_SERVER_STREAMSERVER_H
#define TWILIGHT_SERVER_STREAMSERVER_H

#include "CapturePipeline.h"

#include "common/Rational.h"
#include "common/log.h"

#include "common/net/NetworkServer.h"

#include "server/AudioEncoder.h"
#include "server/Connection.h"
#include "server/KnownClients.h"
#include "server/LocalClock.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

class StreamServer {
public:
    StreamServer();
    ~StreamServer();

    void start();
    void stop();

    void getNativeMode(int* w, int* h, Rational* fps);

    void onDisconnected(Connection* conn);
    void configureStream(Connection* conn, int width, int height, Rational framerate);
    bool startStream(Connection* conn);
    void endStream(Connection* conn);

    const LocalClock& getClock() const { return clock; }

    ByteBuffer getLocalCert();

    std::vector<CertHash> listKnownClients() const { return knownClients.list(); }

    // FIXME: Public access to property
    KnownClients knownClients;

private:
    LoggerPtr log;

    NetworkServer server;

    std::mutex connectionsLock;

    // FIXME: This is the most ugly design I've ever written: Having a dedicated thread to join threads
    std::deque<Connection*> deleteReq;
    std::condition_variable deleteReqCV;
    std::thread deleterThread;
    std::atomic<bool> flagRunDeleter;

    int requestedWidth;
    int requestedHeight;
    Rational requestedFramerate;
    bool streaming;

    LocalClock clock;

    AudioEncoder audioEncoder;

    std::vector<std::unique_ptr<Connection>> connections;

    std::unique_ptr<CapturePipeline> capture;
    std::shared_ptr<CursorPos> cursorPos;

    std::chrono::steady_clock::time_point lastStatReport;

    void processOutput_(DesktopFrame<ByteBuffer>&& cap);
    void broadcast_(const msg::Packet& pkt, const uint8_t* extraData);
    void broadcast_(const msg::Packet& pkt, const ByteBuffer& extraData);
};

#endif