#include "StreamClient.h"

#include "common/util.h"

#include <mbedtls/sha512.h>
#include <mbedtls/sha256.h>

#include <packet.pb.h>
#include <auth.pb.h>


constexpr uint16_t SERVICE_PORT = 6495;
constexpr int32_t AUTH_PROTOCOL_VERSION = 1;


StreamClient::StreamClient() :
	log(createNamedLogger("StreamClient"))
{
    conn.setOnDisconnected([this](std::string_view msg) {
        onStateChange(State::DISCONNECTED, msg);
    });

    keypair.loadOrGenerate("privkey.der");
}

StreamClient::~StreamClient() {
}

void StreamClient::connect(HostListEntry host) {
    if (host->hasClientCert()) {
        bool removeCert = !host->hasServerCert();

        if (host->hasServerCert()) {
            int ret;
            uint32_t flags;
            ret = mbedtls_x509_crt_verify(&host->clientCert, &host->serverCert, nullptr, nullptr, &flags, nullptr, nullptr);
            if (ret < 0)
                removeCert = true;
        }

        if (removeCert) {
            mbedtls_x509_crt_free(&host->clientCert);
            mbedtls_x509_crt_init(&host->clientCert);
        }
    }

    bool needsAuth = !host->hasClientCert();

    if (host->hasServerCert())
        conn.setRemoteCert(&host->serverCert);

    if (host->hasClientCert())
        conn.setLocalCert(&host->clientCert, keypair.pk());

    recvThread = std::thread([this, host, needsAuth]() {
        if (conn.connect(host->addr[0].c_str(), SERVICE_PORT)) {
            if (needsAuth || !conn.verifyCert()) {
                onStateChange(State::AUTHENTICATING, "");
                if (!doAuth_(host))
                    onStateChange(State::DISCONNECTED, "Auth failed"); //TODO: Find a way to localize this
            }
            onStateChange(State::CONNECTED, "");
            _runRecv();
        }
        else
            onStateChange(State::DISCONNECTED, "Unable to connect"); //TODO: Pass message from OS
    });
}

void StreamClient::disconnect() {
    conn.disconnect();
    recvThread.join();
}

void StreamClient::_runRecv() {
    bool stat;
    msg::Packet pkt;
    ByteBuffer extraData;

    while (true) {
        stat = conn.recv(&pkt, &extraData);
        if (!stat)
            break;

        onNextPacket(pkt, extraData.data());
    }

    log->info("Stopping receive loop");
}

// Returns negative on error (mbedtls error code)
static int computePin(const ByteBuffer& serverPubkey, const ByteBuffer& clientPubkey,
    const ByteBuffer& serverNonce, const ByteBuffer& clientNonce) {

    static_assert(std::numeric_limits<int>::max() > 99999999, "Int is too small to compute pin!");

    int ret;
    ByteBuffer payload;
    ByteBuffer hash(64);

    payload.reserve(serverPubkey.size() + clientPubkey.size() + serverNonce.size() + clientNonce.size());
    payload.append(serverPubkey);
    payload.append(clientPubkey);
    payload.append(serverNonce);
    payload.append(clientNonce);

    ret = mbedtls_sha256_ret(payload.data(), payload.size(), hash.data(), 0);
    if (ret < 0)
        return ret;

    uint64_t value = (uint64_t) hash[0] | (uint64_t) hash[1] << 8 |
        (uint64_t) hash[2] << 16 | (uint64_t) hash[3] << 24 |
        (uint64_t) hash[4] << 32 | (uint64_t) hash[5] << 40 |
        (uint64_t) hash[6] << 48 | (uint64_t) hash[7] << 56;

    int output = 0;
    for (int i = 0; i < 8; i++) {
        output *= 10;
        output += value % 10;
        value /= 10;
    }

    return output;
}

bool StreamClient::doAuth_(HostListEntry host) {
    bool status;
    int ret;
    msg::Packet pkt;

    ByteBuffer nonce(16), serverNonce;

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, nullptr, 0);
    check_quit(ret < 0, log, "Failed to seed ctr_drbg");

    ret = mbedtls_ctr_drbg_random(&ctr_drbg, nonce.data(), nonce.size());
    check_quit(ret < 0, log, "Failed to get random nonce");

    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    ByteBuffer serverPubkey = conn.getRemotePubkey();
    ByteBuffer clientPubkey = keypair.pubkey();

    ByteBuffer nonceHashData;
    nonceHashData.reserve(serverPubkey.size() + clientPubkey.size() + nonce.size());
    nonceHashData.append(serverPubkey);
    nonceHashData.append(clientPubkey);
    nonceHashData.append(nonce);

    ByteBuffer payload;
    payload.reserve(std::min<size_t>(64, 48 + clientPubkey.size()));
    ret = mbedtls_sha512_ret(nonceHashData.data(), nonceHashData.size(), payload.data(), 1);
    check_quit(ret < 0, log, "Failed to calculate nonce hash: {}", interpretMbedtlsError(ret));

    // Truncate to SHA-384
    payload.resize(48);
    payload.append(clientPubkey);

    auto signReq = pkt.mutable_sign_request();
    *signReq->mutable_hostname() = "CLIENT-TESTING";
    signReq->set_auth_protocol_version(AUTH_PROTOCOL_VERSION);
    pkt.set_extra_data_len(payload.size());
    status = conn.send(pkt, payload);
    if (!status)
        return false;

    status = conn.recv(&pkt, &serverNonce);
    if (!status || pkt.msg_case() != msg::Packet::kServerNonceNotify)
        return false;

    pkt.mutable_client_nonce_notify();
    pkt.set_extra_data_len(nonce.size());
    status = conn.send(pkt, nonce);
    if (!status)
        return false;

    int pin = computePin(serverPubkey, clientPubkey, serverNonce, nonce);
    if (pin < 0) {
        log->error("Failed to compute pin: {}", interpretMbedtlsError(pin));
        return false;
    }

    status = conn.recv(&pkt, nullptr);
    if (!status || pkt.msg_case() != msg::Packet::kServerPinReady)
        return false;

    log->info("Pin: {}", pin);
    if(onDisplayPin)
        onDisplayPin(pin);

    status = conn.recv(&pkt, &payload);
    if (!status || pkt.msg_case() != msg::Packet::kSignResponse)
        return false;

    auto* signResponse = pkt.mutable_sign_response();
    if (signResponse->status() != msg::SignResponse_AuthStatus_SUCCESS)
        return false;

    ByteBuffer serverCert = conn.getRemoteCert();
    ret = mbedtls_x509_crt_parse(&host->serverCert, serverCert.data(), serverCert.size());
    if (ret < 0) {
        log->error("Failed to parse server certificate: {}", interpretMbedtlsError(ret));
        return ret;
    }

    ret = mbedtls_x509_crt_parse(&host->clientCert, payload.data(), payload.size());
    if (ret < 0) {
        log->error("Failed to parse received certificate: {}", interpretMbedtlsError(ret));
        return false;
    }

    log->info("Successfully received certificate");

    return true;
}
