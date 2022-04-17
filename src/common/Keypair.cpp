#include "Keypair.h"

#include "common/util.h"

#include <mbedtls/asn1.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/sha256.h>

#include <cstdio>
#include <cstring>

TWILIGHT_DEFINE_LOGGER(Keypair);

Keypair::Keypair() {
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
        log.info("Generating keypair... (Reason: {})", mbedtls_error{ret});

        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);

        const char *pers = "twilight-keygen";
        const unsigned char *persPtr = reinterpret_cast<const unsigned char *>(pers);
        ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, persPtr, strlen(pers));

        const mbedtls_pk_info_t *info = mbedtls_pk_info_from_type(MBEDTLS_PK_RSA);
        ret = mbedtls_pk_setup(&ctx, info);
        log.assert_quit(0 <= ret, "Failed to setup pk: {}", mbedtls_error{ret});

        ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(ctx), mbedtls_ctr_drbg_random, &ctr_drbg, 2048, 65537);
        log.assert_quit(0 <= ret, "Failed to generate RSA keypair: {}", mbedtls_error{ret});

        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);

        ByteBuffer der;
        der.resize(512);

        while (true) {
            ret = mbedtls_pk_write_key_der(&ctx, der.data(), der.size());
            if (ret == MBEDTLS_ERR_ASN1_BUF_TOO_SMALL) {
                der.resize(der.size() * 2);
                continue;
            }
            log.assert_quit(0 <= ret, "Failed to serialize pk: {}", mbedtls_error{ret});
            break;
        }

        der.shiftTowardBegin(der.size() - ret);
        der.resize(ret);

        status = writeByteBuffer(filename, der);
        if (!status)
            log.error("Unknown error while writing private key");
    }
}

bool Keypair::parse(const ByteBuffer &key) {
    int ret;

    ret = mbedtls_pk_parse_key(&ctx, key.data(), key.size(), nullptr, 0);
    if (ret < 0) {
        log.error("Failed to parse private key: {}", mbedtls_error{ret});
        return false;
    }
    return true;
}

bool Keypair::parsePubkey(const ByteBuffer &key) {
    int ret;

    ret = mbedtls_pk_parse_public_key(&ctx, key.data(), key.size());
    if (ret < 0) {
        log.error("Failed to parse public key: {}", mbedtls_error{ret});
        return false;
    }
    return true;
}

ByteBuffer Keypair::privkey() const {
    ByteBuffer data;
    data.resize(512);

    mbedtls_pk_context *ptr = const_cast<mbedtls_pk_context *>(&ctx);
    int ret;

    while (true) {
        ret = mbedtls_pk_write_key_der(ptr, data.data(), data.size());
        if (ret == MBEDTLS_ERR_ASN1_BUF_TOO_SMALL) {
            data.resize(data.size() * 2);
            continue;
        }
        log.assert_quit(0 <= ret, "Failed to serialize private key: {}", mbedtls_error{ret});
        break;
    }

    data.shiftTowardBegin(data.size() - ret);
    data.resize(ret);
    data.shrinkToFit();

    return data;
}

ByteBuffer Keypair::pubkey() const {
    ByteBuffer data;
    data.resize(512);

    mbedtls_pk_context *ptr = const_cast<mbedtls_pk_context *>(&ctx);
    int ret;

    while (true) {
        ret = mbedtls_pk_write_pubkey_der(ptr, data.data(), data.size());
        if (ret == MBEDTLS_ERR_ASN1_BUF_TOO_SMALL) {
            data.resize(data.size() * 2);
            continue;
        }
        log.assert_quit(0 <= ret, "Failed to serialize public key: {}", mbedtls_error{ret});
        break;
    }

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
    log.assert_quit(0 <= ret, "Failed to compute SHA-265 fingerprint: {}", mbedtls_error{ret});

    return hash;
}
