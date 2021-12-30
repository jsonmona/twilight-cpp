#ifndef TWILIGHT_SERVER_STREAMSERVER_H
#define TWILIGHT_SERVER_STREAMSERVER_H

#include "CapturePipeline.h"

#include "common/log.h"

#include "common/net/NetworkServer.h"

#include "server/AudioEncoder.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

class StreamServer {
public:
    StreamServer();
    ~StreamServer();

    void start();
    void stop();

private:
    LoggerPtr log;

    NetworkServer server;
    std::unique_ptr<NetworkSocket> conn;

    AudioEncoder audioEncoder;

    std::unique_ptr<CapturePipeline> capture;
    std::shared_ptr<CursorPos> cursorPos;

    std::chrono::steady_clock::time_point lastStatReport;

    bool doAuth_();
    void _processOutput(DesktopFrame<ByteBuffer>&& cap);
};

#endif