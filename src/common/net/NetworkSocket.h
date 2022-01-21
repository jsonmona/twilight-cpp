#ifndef TWILIGHT_COMMON_NET_NETWORKSOCKET_H
#define TWILIGHT_COMMON_NET_NETWORKSOCKET_H

#include "common/ByteBuffer.h"
#include "common/CertHash.h"
#include "common/log.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

#include <packet.pb.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

class NetworkSocket {
public:
    NetworkSocket();
    NetworkSocket(mbedtls_net_context initCtx, const mbedtls_ssl_config *ssl_conf);
    NetworkSocket(const NetworkSocket &copy) = delete;
    NetworkSocket(NetworkSocket &&move) = delete;

    ~NetworkSocket();

    bool connect(const char *addr, uint16_t port);
    bool isConnected() const { return connected.load(std::memory_order_relaxed); }

    bool verifyCert();

    void disconnect();

    template <class Fn>
    void setOnDisconnected(Fn fn) {
        onDisconnected = std::move(fn);
    }

    bool send(const msg::Packet &pkt, const uint8_t *extraData);
    bool send(const msg::Packet &pkt, const ByteBuffer &extraData) { return send(pkt, extraData.data()); }

    bool recv(msg::Packet *pkt, ByteBuffer *extraData);

    void setExpectedRemoteCert(CertHash hash);
    void setExpectedRemoteCert(std::vector<CertHash> &&hashList);

    void setLocalCert(mbedtls_x509_crt *cert, mbedtls_pk_context *privkey) {
        localCert = cert;
        localPrivkey = privkey;
    }

    ByteBuffer getRemotePubkey();  // in DER format
    ByteBuffer getRemoteCert();

private:
    LoggerPtr log;

    mbedtls_net_context ctx;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_x509_crt *localCert;
    mbedtls_pk_context *localPrivkey;

    ByteBuffer sendBuffer;
    std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> zeroCopyInputStream;
    std::unique_ptr<google::protobuf::io::CodedInputStream> inputStream;

    std::vector<int> allowedCiphersuites;
    std::vector<CertHash> expectedRemoteCerts;

    std::atomic<bool> connected;
    std::mutex sendLock;
    std::mutex recvLock;

    std::function<void(std::string_view msg)> onDisconnected;

    void reportDisconnected(int errnum);
};

#endif