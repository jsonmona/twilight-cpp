#ifndef TWILIGHT_SERVER_CONNECTION_H
#define TWILIGHT_SERVER_CONNECTION_H

#include "common/log.h"

#include "common/net/NetworkSocket.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

class StreamServer;
struct AuthState;

class Connection {
public:
    Connection(StreamServer* parent, std::unique_ptr<NetworkSocket>&& sock_);
    ~Connection();

    void disconnect();

    bool send(const msg::Packet& pkt, const uint8_t* extraData) { return sock->send(pkt, extraData); }
    bool send(const msg::Packet& pkt, const ByteBuffer& extraData) { return sock->send(pkt, extraData); }

private:
    LoggerPtr log;
    StreamServer* server;
    std::unique_ptr<NetworkSocket> sock;
    std::unique_ptr<AuthState> authState;

    std::thread runThread;

    bool authorized;
    bool streaming;

    void run_();
    void msg_clientIntro_(const msg::ClientIntro& req);
    void msg_queryHostCapsRequest_(const msg::QueryHostCapsRequest& req);
    void msg_configureStreamRequest_(const msg::ConfigureStreamRequest& req);
    void msg_startStreamRequest_(const msg::StartStreamRequest& req);
    void msg_stopStreamRequest_(const msg::StopStreamRequest& req);

    void msg_authRequest_(const msg::AuthRequest& req, const ByteBuffer& extraData);
    void msg_clientNonceNotify_(const msg::ClientNonceNotify& req, const ByteBuffer& extraData);
};

#endif