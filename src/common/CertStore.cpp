#include "CertStore.h"

#include "common/util.h"

#include <cstdio>
#include <ctime>

CertStore::CertStore() : log(createNamedLogger("CertStore")) {
    mbedtls_x509_crt_init(&cert_);
}

CertStore::~CertStore() {
    mbedtls_x509_crt_free(&cert_);
}

void CertStore::loadKey(std::unique_ptr<Keypair> &&pk) {
    keypair_ = std::move(pk);
}

void CertStore::loadCert(const char *filename) {
    bool genCert = false;
    int ret;

    check_quit(keypair_ == nullptr, log, "Unable to generate certificate from an empty keypair!");

    ret = mbedtls_x509_crt_parse_file(&cert_, filename);
    if (ret == 0) {
        // Test if certificate matches private key
        ret = mbedtls_pk_check_pair(&cert_.pk, keypair_->pk());
        if (ret != 0)
            log->warn("Certificate does not match private key! Is key regenerated?");
    }
    if (ret < 0) {
        log->info("Generating certificate... (Reason: {})", interpretMbedtlsError(ret));
        genCert = true;
    }

    if (genCert) {
        // Example: O=daylight,OU=<hash>,CN=<hostname>
        std::string subjectName = "O=daylight,OU=";
        subjectName += keypair_->fingerprintSHA256().intoHexString();
        subjectName += ",CN=";
        subjectName += "hostname";

        ByteBuffer der = genCert_(subjectName.c_str(), subjectName.c_str(), keypair_->pk(), keypair_->pk());

        ret = mbedtls_x509_crt_parse_der(&cert_, der.data(), der.size());
        check_quit(ret < 0, log, "Failed to read generated certificate");

        writeByteBuffer("cert.der", der);
    }
}

ByteBuffer CertStore::signCert(const char *subjectName, mbedtls_pk_context *subjectKey) {
    int ret;
    ByteBuffer issuerName;
    issuerName.resize(128);

    while (true) {
        ret = mbedtls_x509_dn_gets(issuerName.data_char(), issuerName.size(), &cert()->subject);
        check_quit(ret < 0, log, "Failed to get DN");

        if (ret >= issuerName.size() - 1)
            issuerName.resize(issuerName.size() * 2);
        else {
            issuerName.resize(ret);
            break;
        }
    }

    return genCert_(subjectName, issuerName.data_char(), subjectKey, keypair_->pk());
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

    const char *pers = "daylight-certgen";
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
    check_quit(ret < 0, log, "Failed to set serial");
    mbedtls_mpi_free(&serial);

    ret = mbedtls_x509write_crt_set_subject_name(&ctx, subjectName);
    check_quit(ret < 0, log, "Failed to set subject name: {}", interpretMbedtlsError(ret));

    ret = mbedtls_x509write_crt_set_issuer_name(&ctx, issuerName);
    check_quit(ret < 0, log, "Failed to set issuer name: {}", interpretMbedtlsError(ret));

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
    check_quit(ret < 0, log, "Failed to set validity: {}", interpretMbedtlsError(ret));

    if (isSelfsign)
        ret = mbedtls_x509write_crt_set_basic_constraints(&ctx, 1, -1);
    else
        ret = mbedtls_x509write_crt_set_basic_constraints(&ctx, 0, -1);
    check_quit(ret < 0, log, "Failed to set basic constraints: {}", interpretMbedtlsError(ret));

    ByteBuffer der;
    der.resize(2048);
    ret = mbedtls_x509write_crt_der(&ctx, der.data(), der.size(), mbedtls_ctr_drbg_random, &ctr_drbg);
    check_quit(ret < 0, log, "Failed to serialize X.509 certificate: {}", interpretMbedtlsError(ret));

    mbedtls_x509write_crt_free(&ctx);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    der.shiftTowardBegin(der.size() - ret);
    der.resize(ret);
    der.shrinkToFit();
    return der;
}