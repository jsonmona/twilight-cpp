#include "NetworkServer.h"

#include "common/util.h"

#include <mbedtls/ssl.h>
#include <mbedtls/ssl_internal.h>

#include <string>

// Mozilla Intermediate SSL but prefers chacha20 over AES
constexpr static std::string_view ALLOWED_CIPHERS =
    "\
TLS-ECDHE-ECDSA-WITH-CHACHA20-POLY1305-SHA256:\
TLS-ECDHE-RSA-WITH-CHACHA20-POLY1305-SHA256:\
TLS-ECDHE-ECDSA-WITH-AES-128-GCM-SHA256:\
TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256:\
TLS-ECDHE-ECDSA-WITH-AES-256-GCM-SHA384:\
TLS-ECDHE-RSA-WITH-AES-256-GCM-SHA384:\
TLS-DHE-RSA-WITH-CHACHA20-POLY1305-SHA256:\
TLS-DHE-RSA-WITH-AES-128-GCM-SHA256:\
TLS-DHE-RSA-WITH-AES-256-GCM-SHA384";

#define TWILIGHT_WRITE_SSLKEYLOG

#ifdef TWILIGHT_WRITE_SSLKEYLOG
static int exportKeysCb(void *p_expkey, const unsigned char *ms, const unsigned char *kb, size_t maclen, size_t keylen,
                        size_t ivlen, const unsigned char client_random[32], const unsigned char server_random[32],
                        mbedtls_tls_prf_types tls_prf_type) {
    ByteBuffer clientRandom;
    clientRandom.append(client_random, 32);

    ByteBuffer masterKey;
    masterKey.append(ms, 48);

    FILE *f = fopen("sslkey.log", "ab");
    fprintf(f, "CLIENT_RANDOM %s %s\n", clientRandom.intoHexString().c_str(), masterKey.intoHexString().c_str());
    fclose(f);

    return 0;
}
#endif

NetworkServer::NetworkServer() : log(createNamedLogger("NetworkServer")) {
    mbedtls_net_init(&ctx);
    mbedtls_ssl_config_init(&ssl);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    std::unique_ptr<Keypair> keypair = std::make_unique<Keypair>();
    keypair->loadOrGenerate("privkey.der");
    certStore.loadKey(std::move(keypair));
    certStore.loadCert("cert.der");

    stringSplit(ALLOWED_CIPHERS, ':', [&](std::string_view slice) {
        std::string name(slice.begin(), slice.end());
        const mbedtls_ssl_ciphersuite_t *ptr = mbedtls_ssl_ciphersuite_from_string(name.c_str());
        if (ptr != nullptr)
            allowedCiphersuites.push_back(ptr->id);
        else
            log->warn("Unknown cipher suite name {}", name);
    });
    allowedCiphersuites.push_back(0);  // Terminator
    allowedCiphersuites.shrink_to_fit();

    int stat;
    const char *pers = "twilight-server";
    stat = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, reinterpret_cast<const uint8_t *>(pers),
                                 strlen(pers));
    check_quit(stat < 0, log, "Failed to seed ctr_drbg: {}", interpretMbedtlsError(stat));

    mbedtls_ssl_config_defaults(&ssl, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_authmode(&ssl, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_cert_req_ca_list(&ssl, MBEDTLS_SSL_CERT_REQ_CA_LIST_DISABLED);
    mbedtls_ssl_conf_ca_chain(&ssl, nullptr, nullptr);
    mbedtls_ssl_conf_rng(&ssl, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_conf_ciphersuites(&ssl, allowedCiphersuites.data());
    mbedtls_ssl_conf_min_version(&ssl, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
#ifdef TWILIGHT_WRITE_SSLKEYLOG
    mbedtls_ssl_conf_export_keys_ext_cb(&ssl, exportKeysCb, nullptr);
#endif
    // TODO: Setup session cache

    stat = mbedtls_ssl_conf_own_cert(&ssl, certStore.cert(), certStore.keypair().pk());
    check_quit(stat < 0, log, "Failed to set own cert: {}", interpretMbedtlsError(stat));
}

NetworkServer::~NetworkServer() {
    flagListen.store(false, std::memory_order_release);
    mbedtls_net_free(&ctx);

    if (listenThread.joinable())
        listenThread.join();

    mbedtls_ssl_config_free(&ssl);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
}

void NetworkServer::startListen(uint16_t port) {
    bool prev = flagListen.exchange(true, std::memory_order_acq_rel);
    check_quit(prev, log, "Starting listening again when already listening");

    char portString[8] = {};
    sprintf(portString, "%d", port);
    static_assert(sizeof(port) == 2, "sprintf above is safe because port is u16");

    // FIXME: Can socket be bound twice?
    int stat = mbedtls_net_bind(&ctx, "0.0.0.0", portString, MBEDTLS_NET_PROTO_TCP);
    check_quit(stat != 0, log, "Failed to bind socket");

    // FIXME: Is this really needed?
    if (listenThread.joinable())
        listenThread.join();

    listenThread = std::thread([this, port]() {
        while (flagListen.load(std::memory_order_acquire)) {
            mbedtls_net_context client;
            mbedtls_net_init(&client);
            int stat = mbedtls_net_accept(&ctx, &client, nullptr, 0, nullptr);
            if (stat != 0) {
                mbedtls_net_free(&client);
                break;
            }

            onNewConnection(std::make_unique<NetworkSocket>(client, &ssl));
        }
    });
}

void NetworkServer::stopListen() {
    flagListen.store(false, std::memory_order_release);
    mbedtls_net_free(&ctx);

    if (listenThread.joinable())
        listenThread.join();
}