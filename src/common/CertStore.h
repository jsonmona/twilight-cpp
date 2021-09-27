#ifndef COMMON_CERT_STORE_H_
#define COMMON_CERT_STORE_H_


#include "common/ByteBuffer.h"
#include "common/log.h"

#include <mbedtls/ssl.h>
#include <mbedtls/x509.h>
#include <mbedtls/pk.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>


class CertStore {
public:
	CertStore();
	CertStore(const CertStore& copy) = delete;
	CertStore(CertStore&& move) = delete;
	~CertStore();

	mbedtls_pk_context* getPrivkey() { return &privkey; }
	mbedtls_x509_crt* getCert() { return &cert; }

private:
	LoggerPtr log;
	mbedtls_pk_context privkey;
	mbedtls_x509_crt cert;

	void init_();
	void loadKey_(mbedtls_ctr_drbg_context* ctr_drbg);
	void loadCert_(mbedtls_ctr_drbg_context* ctr_drbg);
};


#endif