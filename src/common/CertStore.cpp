#include "CertStore.h"

#include "common/util.h"

#include <cstdio>
#include <ctime>

TWILIGHT_DEFINE_LOGGER(CertStore);

CertStore::CertStore() {
    mbedtls_x509_crt_init(&cert_);
}

CertStore::~CertStore() {
    mbedtls_x509_crt_free(&cert_);
}

void CertStore::loadKey(std::unique_ptr<Keypair> &&pk) {
    keypair_ = std::move(pk);
}

void CertStore::loadCert(const char *filename) {
    int ret;

    log.assert_quit(keypair_ != nullptr, "Unable to generate certificate from an empty keypair!");

    ret = mbedtls_x509_crt_parse_file(&cert_, filename);
    if (ret == 0) {
        // Test if certificate matches private key
        ret = mbedtls_pk_check_pair(&cert_.pk, keypair_->pk());
        if (ret != 0)
            log.warn("Certificate does not match private key! Is key regenerated?");
    }
    if (ret < 0) {
        log.info("Generating certificate... (Reason: {})", mbedtls_error{ret});

        mbedtls_x509_crt_free(&cert_);
        mbedtls_x509_crt_init(&cert_);

        // Example: O=twilight,OU=<hash>,CN=<hostname>
        std::string subjectName = "O=twilight,OU=";
        subjectName += keypair_->fingerprintSHA256().intoHexString();
        subjectName += ",CN=";
        subjectName += "localhost";

        ByteBuffer der = genCert_(subjectName.c_str(), subjectName.c_str(), keypair_->pk(), keypair_->pk());

        ret = mbedtls_x509_crt_parse_der(&cert_, der.data(), der.size());
        log.assert_quit(0 <= ret, "Failed to read generated certificate");

        writeByteBuffer("cert.der", der);
    }
}

ByteBuffer CertStore::signCert(const char *subjectName, mbedtls_pk_context *subjectKey) {
    int ret;
    ByteBuffer issuerName;
    issuerName.resize(128);

    while (true) {
        ret = mbedtls_x509_dn_gets(issuerName.data_char(), issuerName.size(), &cert()->subject);
        log.assert_quit(0 <= ret, "Failed to get DN");

        if (ret >= issuerName.size() - 1)
            issuerName.resize(issuerName.size() * 2);
        else {
            issuerName.resize(ret);
            break;
        }
    }

    return genCert_(subjectName, issuerName.data_char(), subjectKey, keypair_->pk());
}

ByteBuffer CertStore::der() {
    ByteBuffer ret;
    ret.append(cert_.raw.p, cert_.raw.len);
    return ret;
}

ByteBuffer CertStore::genCert_(const char *subjectName, const char *issuerName, mbedtls_pk_context *subjectKey,
                               mbedtls_pk_context *issuerKey) {
    int ret;

    bool isSelfsign = strcmp(subjectName, issuerName) == 0;

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509write_cert ctx;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_x509write_crt_init(&ctx);

    const char *pers = "twilight-certgen";
    const unsigned char *persPtr = reinterpret_cast<const unsigned char *>(pers);
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, persPtr, strlen(pers));

    mbedtls_x509write_crt_set_version(&ctx, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&ctx, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&ctx, subjectKey);
    mbedtls_x509write_crt_set_issuer_key(&ctx, issuerKey);

    mbedtls_mpi serial;
    mbedtls_mpi_init(&serial);
    mbedtls_mpi_read_string(&serial, 10, "1");
    ret = mbedtls_x509write_crt_set_serial(&ctx, &serial);
    log.assert_quit(0 <= ret, "Failed to set serial: {}", mbedtls_error{ret});
    mbedtls_mpi_free(&serial);

    ret = mbedtls_x509write_crt_set_subject_name(&ctx, subjectName);
    log.assert_quit(0 <= ret, "Failed to set subject name: {}", mbedtls_error{ret});

    ret = mbedtls_x509write_crt_set_issuer_name(&ctx, issuerName);
    log.assert_quit(0 <= ret, "Failed to set issuer name: {}", mbedtls_error{ret});

    const time_t nowTimestamp = time(nullptr);
    tm *timeInfo = gmtime(&nowTimestamp);
    if (timeInfo->tm_sec > 59)
        timeInfo->tm_sec = 59;
    char notBefore[16];
    char notAfter[16];
    strftime(notBefore, sizeof(notBefore), "%Y%m%d%H%M%S", timeInfo);
    timeInfo->tm_year += 500;
    strftime(notAfter, sizeof(notAfter), "%Y%m%d%H%M%S", timeInfo);
    ret = mbedtls_x509write_crt_set_validity(&ctx, notBefore, notAfter);
    log.assert_quit(0 <= ret, "Failed to set validity: {}", mbedtls_error{ret});

    if (isSelfsign)
        ret = mbedtls_x509write_crt_set_basic_constraints(&ctx, 1, 2);
    else
        ret = mbedtls_x509write_crt_set_basic_constraints(&ctx, 0, -1);

    log.assert_quit(0 <= ret, "Failed to set basic constraints: {}", mbedtls_error{ret});

    ByteBuffer der;
    der.resize(512);
    while (true) {
        ret = mbedtls_x509write_crt_der(&ctx, der.data(), der.size(), mbedtls_ctr_drbg_random, &ctr_drbg);
        if (ret == MBEDTLS_ERR_ASN1_BUF_TOO_SMALL) {
            der.resize(der.size() * 2);
            continue;
        }
        log.assert_quit(0 <= ret, "Failed to serialize X.509 certificate: {}", mbedtls_error{ret});
        break;
    }

    mbedtls_x509write_crt_free(&ctx);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    der.shiftTowardBegin(der.size() - ret);
    der.resize(ret);
    der.shrinkToFit();
    return der;
}
