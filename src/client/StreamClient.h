#ifndef TWILIGHT_CLIENT_STREAMCLIENT_H
#define TWILIGHT_CLIENT_STREAMCLIENT_H

#include "common/CertStore.h"
#include "common/log.h"

#include "common/net/NetworkSocket.h"

#include "client/HostList.h"
#include "client/NetworkClock.h"

#include <packet.pb.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class StreamClient {
public:
    enum class State : int { CONNECTED, DISCONNECTED, AUTHENTICATING };

    explicit StreamClient(NetworkClock &clock);
    ~StreamClient();

    template <typename Fn>
    void setOnNextPacket(Fn fn) {
        onNextPacket = std::move(fn);
    }

    template <typename Fn>
    void setOnStateChange(Fn fn) {
        onStateChange = std::move(fn);
    }

    template <typename Fn>
    void setOnDisplayPin(Fn fn) {
        onDisplayPin = std::move(fn);
    }

    void connect(HostListEntry host);
    void disconnect();

    void getCaptureResolution(int *width, int *height);

    bool send(const msg::Packet &pkt, const ByteBuffer &extraData);
    bool send(const msg::Packet &pkt, const uint8_t *extraData);

private:
    void runRecv_();
    void runPing_();
    bool doIntro_(const HostListEntry &host, bool forceAuth);
    bool doAuth_(const HostListEntry &host);

    static NamedLogger log;

    NetworkClock &clock;
    int captureWidth, captureHeight;

    NetworkSocket conn;
    CertStore cert;

    std::function<void(const msg::Packet &, uint8_t *)> onNextPacket;
    std::function<void(State, std::string_view msg)> onStateChange;
    std::function<void(int)> onDisplayPin;

    std::atomic<bool> flagRunPing;

    std::thread recvThread;
    std::thread pingThread;
};

#endif
