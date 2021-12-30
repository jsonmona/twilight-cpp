#ifndef TWILIGHT_COMMON_NET_NETWORKSERVER_H
#define TWILIGHT_COMMON_NET_NETWORKSERVER_H

#include "common/CertStore.h"
#include "common/log.h"

#include "common/net/NetworkSocket.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

#include <atomic>
#include <functional>
#include <thread>

class NetworkServer {
public:
    NetworkServer();
    NetworkServer(const NetworkServer &copy) = delete;
    NetworkServer(NetworkServer &&move) = delete;

    ~NetworkServer();

    void startListen(uint16_t port);
    void stopListen();

    CertStore &getCert() { return certStore; }

    template <class Fn>
    void setOnNewConnection(Fn fn) {
        onNewConnection = std::move(fn);
    }

private:
    LoggerPtr log;

    std::atomic<bool> flagListen;
    std::thread listenThread;

    std::vector<int> allowedCiphersuites;

    mbedtls_net_context ctx;
    mbedtls_ssl_config ssl;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    CertStore certStore;

    std::function<void(std::unique_ptr<NetworkSocket> &&)> onNewConnection;
};

#endif