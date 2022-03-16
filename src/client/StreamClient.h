#ifndef TWILIGHT_CLIENT_STREAMCLIENT_H
#define TWILIGHT_CLIENT_STREAMCLIENT_H

#include <packet.pb.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "client/HostList.h"
#include "common/CertStore.h"
#include "common/log.h"
#include "common/net/NetworkSocket.h"

class StreamClient {
public:
    enum class State : int { CONNECTED, DISCONNECTED, AUTHENTICATING };

    StreamClient();
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

    bool send(const msg::Packet &pkt, const ByteBuffer &extraData);
    bool send(const msg::Packet &pkt, const uint8_t *extraData);

private:
    LoggerPtr log;

    NetworkSocket conn;
    CertStore cert;

    std::function<void(const msg::Packet &, uint8_t *)> onNextPacket;
    std::function<void(State, std::string_view msg)> onStateChange;
    std::function<void(int)> onDisplayPin;

    std::thread recvThread;
    void _runRecv();
    bool doIntro_(const HostListEntry &host, bool forceAuth);
    bool doAuth_(const HostListEntry &host);
};

#endif