#include "Connection.h"

#include "common/util.h"
#include "common/version.h"

#include "server/StreamServer.h"

TWILIGHT_DEFINE_LOGGER(Connection);

// FIXME: Deduplicate
static constexpr int PROTOCOL_VERSION = 1;

// Deduplicate with StreamClient.cpp
// Returns negative on error (mbedtls error code)
static int computePin(const ByteBuffer& serverCert, const ByteBuffer& clientCert, const ByteBuffer& serverNonce,
                      const ByteBuffer& clientNonce) {
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

struct AuthState {
    AuthState() : nonce(32) {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);

        const char* pers = "twilight-auth";
        mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, reinterpret_cast<const uint8_t*>(pers),
                              strlen(pers));
    }

    ~AuthState() {
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    }

    std::string hostname;
    ByteBuffer serverCert, clientCert;
    ByteBuffer nonce, clientNonce;
    ByteBuffer clientPartialHash;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
};

Connection::Connection(StreamServer* parent, std::unique_ptr<NetworkSocket>&& sock_)
    : server(parent), sock(std::move(sock_)), authorized(false), streaming(false) {
    runThread = std::thread([this] { run_(); });
}

Connection::~Connection() {
    if (sock->isConnected()) {
        log.warn("Deconstructed while connected");
        sock->disconnect();
    }

    if (runThread.joinable())
        runThread.join();
}

void Connection::disconnect() {
    sock->disconnect();
}

void Connection::run_() {
    sock->setExpectedRemoteCert(server->listKnownClients());
    authorized = sock->verifyCert();

    msg::Packet pkt;
    ByteBuffer data;

    while (sock->isConnected()) {
        if (!sock->recv(&pkt, &data))
            continue;

        if (authState != nullptr && pkt.msg_case() != msg::Packet::kClientNonceNotify)
            authState.reset();

        switch (pkt.msg_case()) {
        case msg::Packet::kClientIntro:
            msg_clientIntro_(pkt.client_intro());
            break;
        case msg::Packet::kPingRequest:
            msg_pingRequest_(pkt.ping_request());
            break;
        case msg::Packet::kQueryHostCapsRequest:
            msg_queryHostCapsRequest_(pkt.query_host_caps_request());
            break;
        case msg::Packet::kConfigureStreamRequest:
            msg_configureStreamRequest_(pkt.configure_stream_request());
            break;
        case msg::Packet::kStartStreamRequest:
            msg_startStreamRequest_(pkt.start_stream_request());
            break;
        case msg::Packet::kStopStreamRequest:
            msg_stopStreamRequest_(pkt.stop_stream_request());
            break;
        case msg::Packet::kAuthRequest:
            msg_authRequest_(pkt.auth_request(), data);
            break;
        case msg::Packet::kClientNonceNotify:
            msg_clientNonceNotify_(pkt.client_nonce_notify(), data);
            break;
        default:
            log.warn("Received unexpected packet: {}", pkt.msg_case());
            break;
        }
    }

    server->onDisconnected(this);
}

void Connection::msg_clientIntro_(const msg::ClientIntro& req) {
    msg::Packet pkt;
    pkt.set_extra_data_len(0);

    auto* res = pkt.mutable_server_intro();
    res->set_protocol_version(PROTOCOL_VERSION);
    *res->mutable_commit_name() = GIT_COMMIT;

    if (PROTOCOL_VERSION != req.protocol_version())
        res->set_status(msg::ServerIntro_Status_VERSION_MISMATCH);
    else if (!authorized)
        res->set_status(msg::ServerIntro_Status_AUTH_REQUIRED);
    else
        res->set_status(msg::ServerIntro_Status_OK);

    send(pkt, nullptr);
}

void Connection::msg_pingRequest_(const msg::PingRequest& req) {
    // Silently ignore ping request from unauthorized clients
    if (!authorized)
        return;

    msg::Packet pkt;
    pkt.set_extra_data_len(0);

    if (req.latency() != 0)
        log.debug("Client estimated network ping: {}", req.latency());

    auto* res = pkt.mutable_ping_response();
    res->set_id(req.id());
    res->set_time(server->getClock().time().count());
    send(pkt, nullptr);
}

void Connection::msg_queryHostCapsRequest_(const msg::QueryHostCapsRequest& req) {
    msg::Packet pkt;
    pkt.set_extra_data_len(0);

    auto* res = pkt.mutable_query_host_caps_response();

    if (!authorized) {
        res->set_status(msg::QueryHostCapsResponse_Status_UNKNOWN);
        send(pkt, nullptr);
        return;
    }

    int nativeWidth, nativeHeight;
    Rational nativeFps;
    server->getNativeMode(&nativeWidth, &nativeHeight, &nativeFps);

    res->set_native_width(nativeWidth);
    res->set_native_height(nativeHeight);
    res->set_native_fps_num(nativeFps.num());
    res->set_native_fps_den(nativeFps.den());

    if (req.codec() == msg::Codec::H264_BASELINE) {
        res->set_status(msg::QueryHostCapsResponse_Status_OK);

        // FIXME: Measure actual limits
        res->set_max_height(16384);
        res->set_max_width(16384);
        res->set_max_fps_num(res->native_fps_num());
        res->set_max_fps_den(res->native_fps_den());
    } else {
        res->set_status(msg::QueryHostCapsResponse_Status_UNSUPPORTED_CODEC);
    }

    send(pkt, nullptr);
}

void Connection::msg_configureStreamRequest_(const msg::ConfigureStreamRequest& req) {
    msg::Packet pkt;
    pkt.set_extra_data_len(0);

    auto* res = pkt.mutable_configure_stream_response();

    if (!authorized) {
        res->set_status(msg::ConfigureStreamResponse_Status_UNKNOWN);
        send(pkt, nullptr);
        return;
    }

    if (streaming) {
        res->set_status(msg::ConfigureStreamResponse_Status_ALREADY_STREAMING);
        send(pkt, nullptr);
        return;
    }

    if (req.codec() != msg::Codec::H264_BASELINE) {
        res->set_status(msg::ConfigureStreamResponse_Status_UNSUPPORTED_CODEC);
        send(pkt, nullptr);
        return;
    }

    if (req.width() <= 0 || req.height() <= 0 || req.fps_num() <= 0 || req.fps_den() <= 0) {
        res->set_status(msg::ConfigureStreamResponse_Status_UNKNOWN);
        send(pkt, nullptr);
        return;
    }

    server->configureStream(this, req.width(), req.height(), Rational(req.fps_num(), req.fps_den()));
    res->set_status(msg::ConfigureStreamResponse_Status_OK);
    send(pkt, nullptr);
}

void Connection::msg_startStreamRequest_(const msg::StartStreamRequest& req) {
    msg::Packet pkt;
    pkt.set_extra_data_len(0);

    auto* res = pkt.mutable_start_stream_response();

    if (!authorized) {
        res->set_status(msg::StartStreamResponse_Status_UNKNOWN);
        send(pkt, nullptr);
        return;
    }

    bool success = server->startStream(this);
    if (success)
        streaming = true;

    res->set_status(success ? msg::StartStreamResponse_Status_OK : msg::StartStreamResponse_Status_UNKNOWN);
    send(pkt, nullptr);
}

void Connection::msg_stopStreamRequest_(const msg::StopStreamRequest& req) {
    server->endStream(this);

    msg::Packet pkt;
    pkt.set_extra_data_len(0);

    auto* res = pkt.mutable_stop_stream_response();

    send(pkt, nullptr);
}

void Connection::msg_authRequest_(const msg::AuthRequest& req, const ByteBuffer& extraData) {
    int err;
    msg::Packet pkt;

    if (req.client_nonce_len() < 16) {
        log.error("Aborting authentication because client nonce is too short");
        pkt.set_extra_data_len(0);
        auto* res = pkt.mutable_auth_response();
        res->set_status(msg::AuthResponse_Status_NONCE_TOO_SHORT);
        sock->send(pkt, nullptr);
        return;
    }

    if (extraData.size() != 48) {
        log.error("Wrong hash size: {}", extraData.size());
        pkt.set_extra_data_len(0);
        auto* res = pkt.mutable_auth_response();
        res->set_status(msg::AuthResponse_Status_UNKNOWN_ERROR);
        sock->send(pkt, nullptr);
        return;
    }

    authState = std::make_unique<AuthState>();
    authState->hostname = req.hostname();
    authState->clientNonce.resize(req.client_nonce_len());
    authState->clientPartialHash = extraData.clone();
    authState->serverCert = server->getLocalCert();
    authState->clientCert = sock->getRemoteCert();

    err = mbedtls_ctr_drbg_random(&authState->ctr_drbg, authState->nonce.data(), authState->nonce.size());
    if (err != 0) {
        log.error("Failed to get random nonce: {}", mbedtls_error{err});
        pkt.set_extra_data_len(0);
        auto* res = pkt.mutable_auth_response();
        res->set_status(msg::AuthResponse_Status_UNKNOWN_ERROR);
        sock->send(pkt, nullptr);
        return;
    }

    ByteBuffer partialContent, partialHash(64);
    partialContent.reserve(authState->serverCert.size() + authState->clientCert.size() + authState->nonce.size());
    partialContent.append(authState->serverCert);
    partialContent.append(authState->clientCert);
    partialContent.append(authState->nonce);

    err = mbedtls_sha512_ret(partialContent.data(), partialContent.size(), partialHash.data(), 1);
    if (err != 0) {
        log.error("Failed to compute SHA-384 partial hash: {}", mbedtls_error{err});
        pkt.set_extra_data_len(0);
        auto* res = pkt.mutable_auth_response();
        res->set_status(msg::AuthResponse_Status_UNKNOWN_ERROR);
        sock->send(pkt, nullptr);
        return;
    }

    // Truncate to SHA-384
    partialHash.resize(48);

    pkt.set_extra_data_len(partialHash.size());
    auto* res = pkt.mutable_server_partial_hash_notify();
    res->set_server_nonce_len(authState->nonce.size());
    sock->send(pkt, partialHash);
}

void Connection::msg_clientNonceNotify_(const msg::ClientNonceNotify& req, const ByteBuffer& extraData) {
    int err;
    msg::Packet pkt;

    if (authState == nullptr) {
        log.error("Received unexpected ClientNonceNotify packet");
        pkt.set_extra_data_len(0);
        auto* res = pkt.mutable_auth_response();
        res->set_status(msg::AuthResponse_Status_UNKNOWN_ERROR);
        sock->send(pkt, nullptr);
        return;
    }

    if (extraData.size() != authState->clientNonce.size()) {
        log.error("Client nonce size differs from previous statement");
        pkt.set_extra_data_len(0);
        auto* res = pkt.mutable_auth_response();
        res->set_status(msg::AuthResponse_Status_UNKNOWN_ERROR);
        sock->send(pkt, nullptr);
        return;
    }

    memcpy(authState->clientNonce.data(), extraData.data(), extraData.size());

    // Check client nonce using partial hash
    ByteBuffer partialContent, partialHash(64);
    partialContent.reserve(authState->serverCert.size() + authState->clientCert.size() + authState->clientNonce.size());
    partialContent.append(authState->serverCert);
    partialContent.append(authState->clientCert);
    partialContent.append(authState->clientNonce);

    err = mbedtls_sha512_ret(partialContent.data(), partialContent.size(), partialHash.data(), 1);
    if (err != 0) {
        log.error("Failed to calculate SHA-384 client partial hash");
        pkt.set_extra_data_len(0);
        auto* res = pkt.mutable_auth_response();
        res->set_status(msg::AuthResponse_Status_UNKNOWN_ERROR);
        sock->send(pkt, nullptr);
        return;
    }

    // Truncate to SHA-384
    partialHash.resize(48);

    if (!secureMemcmp(partialHash.data(), authState->clientPartialHash.data(), partialHash.size())) {
        log.error("Client partial hash does not match");
        pkt.set_extra_data_len(0);
        auto* res = pkt.mutable_auth_response();
        res->set_status(msg::AuthResponse_Status_UNKNOWN_ERROR);
        sock->send(pkt, nullptr);
        return;
    }

    // Notify server nonce
    // Should be displaying pin entry dialog by now, but terminal input is buffered anyway :P
    pkt.set_extra_data_len(authState->nonce.size());
    pkt.mutable_server_nonce_notify();
    if (!sock->send(pkt, authState->nonce))
        return;

    // Get pin input
    printf("Auth requested. Enter pin (or hit enter to cancel): ");
    char inputBuf[256];
    fgets(inputBuf, sizeof(inputBuf) - 1, stdin);
    int enteredPin = inputBuf[0] != '\0' ? 0 : -1;
    for (int i = 0; i < 256 && inputBuf[i] != '\0'; i++) {
        if (inputBuf[i] == ' ' || inputBuf[i] == '\t' || inputBuf[i] == '\n' || inputBuf[i] == '\r')
            continue;
        if (inputBuf[i] < '0' || '9' < inputBuf[i]) {
            enteredPin = -1;
            break;
        }
        enteredPin = enteredPin * 10 + (inputBuf[i] - '0');
    }

    int truePin = computePin(authState->serverCert, authState->clientCert, authState->nonce, authState->clientNonce);

    if (truePin != enteredPin) {
        log.error("Pin does not match! Expected {}, got {}", truePin, enteredPin);
        pkt.set_extra_data_len(0);
        auto* res = pkt.mutable_auth_response();
        res->set_status(msg::AuthResponse_Status_INCORRECT_PIN);
        sock->send(pkt, nullptr);
        return;
    } else {
        log.info("Auth success.");
    }

    server->knownClients.add(CertHash::digest(CertHash::HashType::SHA256, sock->getRemoteCert()));
    server->knownClients.saveFile("clients.toml");

    pkt.set_extra_data_len(0);
    auto* res = pkt.mutable_auth_response();
    res->set_status(msg::AuthResponse_Status_OK);
    sock->send(pkt, nullptr);

    authorized = true;
}
