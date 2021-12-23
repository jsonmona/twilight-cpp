#ifndef COMMON_CERT_STORE_H_
#define COMMON_CERT_STORE_H_

#include "common/ByteBuffer.h"
#include "common/Keypair.h"
#include "common/log.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509.h>

class CertStore {
public:
    CertStore();
    CertStore(const CertStore &copy) = delete;
    CertStore(CertStore &&move) = delete;
    ~CertStore();

    void loadKey(std::unique_ptr<Keypair> &&pk);
    void loadCert(const char *filename);

    ByteBuffer signCert(const char *subjectName, mbedtls_pk_context *subjectKey);

    mbedtls_x509_crt *cert() { return &cert_; }
    Keypair &keypair() { return *keypair_; }

private:
    LoggerPtr log;
    mbedtls_x509_crt cert_;
    std::unique_ptr<Keypair> keypair_;

    ByteBuffer genCert_(const char *subjectName, const char *issuerName, mbedtls_pk_context *subjectKey,
                        mbedtls_pk_context *issuerKey);
};

#endif