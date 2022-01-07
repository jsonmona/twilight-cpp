#include "Keypair.h"

#include "common/util.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/sha256.h>

#include <cstdio>
#include <cstring>

Keypair::Keypair() : log(createNamedLogger("Keypair")) {
    mbedtls_pk_init(&ctx);
}

Keypair::~Keypair() {
    mbedtls_pk_free(&ctx);
}

void Keypair::loadOrGenerate(const char *filename) {
    bool status;
    int ret;

    ret = mbedtls_pk_parse_keyfile(&ctx, filename, nullptr);
    if (ret < 0) {
        log->info("Generating EC keypair... (Reason: {})", interpretMbedtlsError(ret));

        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);

        const char *pers = "twilight-keygen";
        const unsigned char *persPtr = reinterpret_cast<const unsigned char *>(pers);
        ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, persPtr, strlen(pers));

        const mbedtls_pk_info_t *info = mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY);
        ret = mbedtls_pk_setup(&ctx, info);
        check_quit(ret < 0, log, "Failed to setup pk: {}", interpretMbedtlsError(ret));

        ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(ctx), mbedtls_ctr_drbg_random, &ctr_drbg);
        check_quit(ret < 0, log, "Failed to generate secp256r1 key: {}", interpretMbedtlsError(ret));

        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);

        ByteBuffer der;
        der.resize(2048);

        ret = mbedtls_pk_write_key_der(&ctx, der.data(), der.size());
        check_quit(ret < 0, log, "Failed to serialize pk: {}", interpretMbedtlsError(ret));

        der.shiftTowardBegin(der.size() - ret);
        der.resize(ret);

        status = writeByteBuffer(filename, der);
        if (!status)
            log->error("There was an error writing private key");
    }
}

bool Keypair::parse(const ByteBuffer &key) {
    int ret;

    ret = mbedtls_pk_parse_key(&ctx, key.data(), key.size(), nullptr, 0);
    if (ret < 0) {
        log->error("Failed to parse key: {}", interpretMbedtlsError(ret));
        return false;
    }
    return true;
}

bool Keypair::parsePubkey(const ByteBuffer &key) {
    int ret;

    ret = mbedtls_pk_parse_public_key(&ctx, key.data(), key.size());
    if (ret < 0) {
        log->error("Failed to parse key: {}", interpretMbedtlsError(ret));
        return false;
    }
    return true;
}

ByteBuffer Keypair::privkey() const {
    ByteBuffer data;
    data.resize(16384);

    mbedtls_pk_context *ptr = const_cast<mbedtls_pk_context *>(&ctx);

    int ret = mbedtls_pk_write_key_der(ptr, data.data(), data.size());
    check_quit(ret < 0, log, "Failed to serialize privkey: {}", interpretMbedtlsError(ret));

    data.shiftTowardBegin(data.size() - ret);
    data.resize(ret);
    data.shrinkToFit();

    return data;
}

ByteBuffer Keypair::pubkey() const {
    ByteBuffer data;
    data.resize(16384);

    mbedtls_pk_context *ptr = const_cast<mbedtls_pk_context *>(&ctx);

    int ret = mbedtls_pk_write_pubkey_der(ptr, data.data(), data.size());
    check_quit(ret < 0, log, "Failed to serialize pubkey: {}", interpretMbedtlsError(ret));

    data.shiftTowardBegin(data.size() - ret);
    data.resize(ret);
    data.shrinkToFit();

    return data;
}

ByteBuffer Keypair::fingerprintSHA256() const {
    int ret;

    ByteBuffer hash;
    hash.resize(32);

    ByteBuffer key = pubkey();

    ret = mbedtls_sha256_ret(key.data(), key.size(), hash.data(), 0);
    check_quit(ret < 0, log, "Failed to compute SHA256");

    return hash;
}
