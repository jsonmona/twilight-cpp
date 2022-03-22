#include "StreamClient.h"

#include "common/util.h"
#include "common/version.h"

#include <auth.pb.h>
#include <packet.pb.h>

#include <mbedtls/sha512.h>

constexpr uint16_t SERVICE_PORT = 6495;
constexpr int32_t PROTOCOL_VERSION = 1;

StreamClient::StreamClient(NetworkClock &clock) : log(createNamedLogger("StreamClient")), clock(clock) {
    conn.setOnDisconnected([this](std::string_view msg) { onStateChange(State::DISCONNECTED, msg); });

    std::unique_ptr<Keypair> keypair = std::make_unique<Keypair>();
    keypair->loadOrGenerate("privkey.der");
    cert.loadKey(std::move(keypair));
    cert.loadCert("cert.der");
}

StreamClient::~StreamClient() {
    flagRunPing.store(false, std::memory_order_relaxed);
    if (pingThread.joinable())
        pingThread.join();
}

void StreamClient::connect(HostListEntry host) {
    flagRunPing.store(false, std::memory_order_relaxed);
    if (pingThread.joinable())
        pingThread.join();

    bool needsAuth = !host->certHash.isValid();
    host->updateLastConnected();

    conn.setExpectedRemoteCert(host->certHash);
    conn.setLocalCert(cert.cert(), cert.keypair().pk());

    recvThread = std::thread([this, host, needsAuth]() {
        if (conn.connect(host->addr[0].c_str(), SERVICE_PORT)) {
            if (!doIntro_(host, needsAuth || !conn.verifyCert())) {
                onStateChange(State::DISCONNECTED, "Auth failed");  // TODO: Find a way to localize this
                return;
            }

            flagRunPing.store(true, std::memory_order_relaxed);
            pingThread = std::thread(&StreamClient::runPing_, this);

            onStateChange(State::CONNECTED, "");
            runRecv_();
        } else
            onStateChange(State::DISCONNECTED, "Unable to connect");  // TODO: Pass message from OS
    });
}

void StreamClient::disconnect() {
    flagRunPing.store(false);

    conn.disconnect();
    recvThread.join();
}

bool StreamClient::send(const msg::Packet &pkt, const ByteBuffer &extraData) {
    assert(pkt.extra_data_len() == extraData.size());

    return send(pkt, extraData.data());
}

bool StreamClient::send(const msg::Packet &pkt, const uint8_t *extraData) {
    return conn.send(pkt, extraData);
}

void StreamClient::runRecv_() {
    bool stat;
    msg::Packet pkt;
    ByteBuffer extraData;

    while (true) {
        stat = conn.recv(&pkt, &extraData);
        if (!stat)
            break;

        onNextPacket(pkt, extraData.data());
    }
}

void StreamClient::runPing_() {
    while (flagRunPing.load(std::memory_order_relaxed)) {
        uint32_t pingId;
        std::chrono::milliseconds sleepAmount;

        bool sendPing = clock.generatePing(&pingId, &sleepAmount);
        if (!sendPing) {
            std::this_thread::sleep_for(sleepAmount);
            continue;
        }

        msg::Packet pkt;
        pkt.set_extra_data_len(0);

        auto *req = pkt.mutable_ping_request();
        req->set_id(pingId);
        req->set_latency(clock.latency());

        send(pkt, nullptr);
        std::this_thread::sleep_for(sleepAmount);
    }
}

// Returns negative on error (mbedtls error code)
static int computePin(const ByteBuffer &serverCert, const ByteBuffer &clientCert, const ByteBuffer &serverNonce,
                      const ByteBuffer &clientNonce) {
    static_assert(std::numeric_limits<int>::max() > 99999999, "Int is too small to compute pin!");

    int ret;
    ByteBuffer payload;
    ByteBuffer hash(64);

    payload.reserve(serverCert.size() + clientCert.size() + serverNonce.size() + clientNonce.size());
    payload.append(serverCert);
    payload.append(clientCert);
    payload.append(serverNonce);
    payload.append(clientNonce);

    ret = mbedtls_sha512_ret(payload.data(), payload.size(), hash.data(), 0);
    if (ret < 0)
        return ret;

    uint64_t value = (uint64_t)hash[0] | (uint64_t)hash[1] << 8 | (uint64_t)hash[2] << 16 | (uint64_t)hash[3] << 24 |
                     (uint64_t)hash[4] << 32 | (uint64_t)hash[5] << 40 | (uint64_t)hash[6] << 48 |
                     (uint64_t)hash[7] << 56;

    return value % 100000000;
}

bool StreamClient::doIntro_(const HostListEntry &host, bool forceAuth) {
    msg::Packet pkt;

    /* ClientIntro */ {
        pkt.set_extra_data_len(0);
        auto *now = pkt.mutable_client_intro();
        now->set_protocol_version(PROTOCOL_VERSION);
        *now->mutable_commit_name() = GIT_COMMIT;

        if (!conn.send(pkt, nullptr))
            return false;
    }

    bool authRequired = forceAuth;

    if (!conn.recv(&pkt, nullptr))
        return false;

    check_quit(pkt.msg_case() != msg::Packet::kServerIntro, log, "Expected ServerIntro, received {}", pkt.msg_case());
    auto &serverIntro = pkt.server_intro();
    if (serverIntro.status() != msg::ServerIntro_Status_OK) {
        switch (serverIntro.status()) {
        case msg::ServerIntro_Status_VERSION_MISMATCH:
            error_quit(log, "Protocol version mismatch: local={} remote={}", PROTOCOL_VERSION,
                       serverIntro.protocol_version());
            break;
        case msg::ServerIntro_Status_AUTH_REQUIRED:
            authRequired = true;
            break;
        default:
            error_quit(log, "ServerIntro returned with unknown error: {}", serverIntro.status());
            break;
        }
    }

    log->info("Greeted by server version {} ({})", serverIntro.protocol_version(), serverIntro.commit_name());
    if (authRequired) {
        log->info("Auth required...");

        if (!doAuth_(host)) {
            log->warn("Failed to authenticate");
            return false;
        }
    }

    /* QueryHostCapsRequest */ {
        auto *req = pkt.mutable_query_host_caps_request();
        req->set_codec(msg::Codec::H264_BASELINE);

        if (!conn.send(pkt, nullptr))
            return false;
    }

    if (!conn.recv(&pkt, nullptr))
        return false;
    check_quit(pkt.msg_case() != msg::Packet::kQueryHostCapsResponse, log,
               "Expected QueryHostCapsResponse, received {}", pkt.msg_case());

    auto &hostcaps = pkt.query_host_caps_response();
    if (hostcaps.status() != msg::QueryHostCapsResponse_Status_OK)
        error_quit(log, "Failed to query host caps");

    int nativeWidth = hostcaps.native_width();
    int nativeHeight = hostcaps.native_height();
    int nativeFpsNum = hostcaps.native_fps_num();
    int nativeFpsDen = hostcaps.native_fps_den();

    /* ConfigureStreamRequest */ {
        auto *req = pkt.mutable_configure_stream_request();
        req->set_codec(msg::Codec::H264_BASELINE);
        req->set_width(nativeWidth);
        req->set_height(nativeHeight);
        req->set_fps_num(nativeFpsNum);
        req->set_fps_den(nativeFpsDen);

        if (!conn.send(pkt, nullptr))
            return false;
    }

    if (!conn.recv(&pkt, nullptr))
        return false;

    check_quit(pkt.msg_case() != msg::Packet::kConfigureStreamResponse, log,
               "Expected ConfigureStreamResponse, received {}", pkt.msg_case());
    auto &configureStreamResponse = pkt.configure_stream_response();

    if (configureStreamResponse.status() != msg::ConfigureStreamResponse_Status_OK) {
        switch (configureStreamResponse.status()) {
        case msg::ConfigureStreamResponse_Status_UNSUPPORTED_CODEC:
            error_quit(log, "Failed to configure stream due to unsupported codec");
            break;
        default:
            error_quit(log, "Failed to configure stream due to an unknown error");
            break;
        }
    }

    pkt.mutable_start_stream_request();
    if (!conn.send(pkt, nullptr))
        return false;

    if (!conn.recv(&pkt, nullptr))
        return false;

    check_quit(pkt.msg_case() != msg::Packet::kStartStreamResponse, log, "Expected StartStreamResponse, received {}",
               pkt.msg_case());
    auto &startStreamResponse = pkt.start_stream_response();

    if (startStreamResponse.status() != msg::StartStreamResponse_Status_OK)
        return false;

    return true;
}

bool StreamClient::doAuth_(const HostListEntry &host) {
    bool status;
    int ret;
    msg::Packet pkt;

    ByteBuffer nonce(32), serverNonce;
    ByteBuffer serverPartialHash;

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

    ByteBuffer serverCert = conn.getRemoteCert();
    ByteBuffer clientCert = cert.der();

    ByteBuffer nonceHashData;
    nonceHashData.reserve(serverCert.size() + clientCert.size() + nonce.size());
    nonceHashData.append(serverCert);
    nonceHashData.append(clientCert);
    nonceHashData.append(nonce);

    ByteBuffer payload;
    payload.reserve(64);
    ret = mbedtls_sha512_ret(nonceHashData.data(), nonceHashData.size(), payload.data(), 1);
    check_quit(ret < 0, log, "Failed to calculate nonce hash: {}", interpretMbedtlsError(ret));

    // Truncate to SHA-384
    payload.resize(48);

    auto signReq = pkt.mutable_auth_request();
    signReq->set_client_nonce_len(nonce.size());
    *signReq->mutable_hostname() = "localhost";
    pkt.set_extra_data_len(payload.size());
    status = conn.send(pkt, payload);
    if (!status)
        return false;

    status = conn.recv(&pkt, &serverPartialHash);
    if (!status || pkt.msg_case() != msg::Packet::kServerPartialHashNotify)
        return false;

    int serverNonceLen = pkt.server_partial_hash_notify().server_nonce_len();
    if (serverNonceLen < 16)
        return false;

    pkt.mutable_client_nonce_notify();
    pkt.set_extra_data_len(nonce.size());
    status = conn.send(pkt, nonce);
    if (!status)
        return false;

    status = conn.recv(&pkt, &serverNonce);
    if (!status || pkt.msg_case() != msg::Packet::kServerNonceNotify)
        return false;

    int pin = computePin(serverCert, clientCert, serverNonce, nonce);
    if (pin < 0) {
        log->error("Failed to compute pin: {}", interpretMbedtlsError(pin));
        return false;
    }

    log->info("Pin: {}", pin);
    if (onDisplayPin)
        onDisplayPin(pin);

    status = conn.recv(&pkt, &payload);
    if (!status || pkt.msg_case() != msg::Packet::kAuthResponse)
        return false;

    auto *signResponse = pkt.mutable_auth_response();
    if (signResponse->status() != msg::AuthResponse_Status_OK)
        return false;

    host->certHash = CertHash::digest(CertHash::HashType::SHA256, serverCert);

    log->info("Auth successful");
    return true;
}
