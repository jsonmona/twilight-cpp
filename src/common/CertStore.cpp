#include "CertStore.h"

#include <cstdio>
#include <ctime>


static size_t writeBinary(const char* filename, const ByteBuffer& data) {
	FILE* f = fopen(filename, "wb");
	size_t offset = 0;
	while (offset < data.size()) {
		size_t write = fwrite(data.data() + offset, 1, data.size() - offset, f);
		if (write == 0)
			break;
		offset += write;
	}
	fclose(f);
	return offset;
}

CertStore::CertStore() :
	log(createNamedLogger("CertStore"))
{
	mbedtls_pk_init(&privkey);
	mbedtls_x509_crt_init(&cert);

	init_();
}

CertStore::~CertStore() {
	//TODO: Is pk zeroized?
	mbedtls_pk_free(&privkey);
	mbedtls_x509_crt_free(&cert);
}

void CertStore::init_() {
	int stat;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;

	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctr_drbg);

	static constexpr char PER_STR[] = "daylight";
	static constexpr int PER_LEN = sizeof(PER_STR) - 1;
	const unsigned char* perString = reinterpret_cast<const unsigned char*>(PER_STR);
	
	stat = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, perString, PER_LEN);
	check_quit(stat < 0, log, "Failed to seed ctr_drbg");

	loadKey_(&ctr_drbg);
	loadCert_(&ctr_drbg);
}

void CertStore::loadKey_(mbedtls_ctr_drbg_context* ctr_drbg) {
	int stat;

	stat = mbedtls_pk_parse_keyfile(&privkey, "privkey.der", nullptr);
	if (stat < 0) {
		log->info("Generating private key... (Reason: {})", interpretMbedtlsError(stat));

		const mbedtls_pk_info_t* info = mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY);
		stat = mbedtls_pk_setup(&privkey, info);
		check_quit(stat < 0, log, "Failed to setup pk: {}", interpretMbedtlsError(stat));

		stat = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(privkey), mbedtls_ctr_drbg_random, ctr_drbg);
		check_quit(stat < 0, log, "Failed to generate secp256r1 key: {}", interpretMbedtlsError(stat));

		ByteBuffer der;
		der.resize(2048);

		stat = mbedtls_pk_write_key_der(&privkey, der.data(), der.size());
		check_quit(stat < 0, log, "Failed to serialize pk: {}", interpretMbedtlsError(stat));

		der.shiftTowardBegin(der.size() - stat);
		der.resize(stat);

		size_t written = writeBinary("privkey.der", der);
		if (written != der.size())
			log->error("There was an error writing private key");
	}
}

void CertStore::loadCert_(mbedtls_ctr_drbg_context* ctr_drbg) {
	int stat;

	stat = mbedtls_x509_crt_parse_file(&cert, "cert.der");
	if (stat == 0) {
		// Test if certificate matches private key
		stat = mbedtls_pk_check_pair(&cert.pk, &privkey);
		if (stat != 0)
			log->warn("Certificate does not match private key! Is key regenerated?");
	}

	if (stat < 0) {
		log->info("Generating certificate... (Reason: {})", interpretMbedtlsError(stat));

		mbedtls_x509write_cert ctx;
		mbedtls_x509write_crt_init(&ctx);

		mbedtls_x509write_crt_set_version(&ctx, MBEDTLS_X509_CRT_VERSION_3);
		mbedtls_x509write_crt_set_md_alg(&ctx, MBEDTLS_MD_SHA256);
		mbedtls_x509write_crt_set_issuer_key(&ctx, &privkey);
		mbedtls_x509write_crt_set_subject_key(&ctx, &privkey);

		mbedtls_mpi serial;
		mbedtls_mpi_init(&serial);
		mbedtls_mpi_read_string(&serial, 10, "1");
		stat = mbedtls_x509write_crt_set_serial(&ctx, &serial);
		check_quit(stat < 0, log, "Failed to set serial");
		mbedtls_mpi_free(&serial);

		std::string subjectName = "O=daylight,CN=";
		subjectName += "DESKTOP-TESTING";
		stat = mbedtls_x509write_crt_set_subject_name(&ctx, subjectName.c_str());
		check_quit(stat < 0, log, "Failed to set subject name: {}", interpretMbedtlsError(stat));

		stat = mbedtls_x509write_crt_set_issuer_name(&ctx, subjectName.c_str());
		check_quit(stat < 0, log, "Failed to set issuer name: {}", interpretMbedtlsError(stat));

		const time_t nowTimestamp = time(nullptr);
		tm* timeInfo = gmtime(&nowTimestamp);
		if (timeInfo->tm_sec > 59)
			timeInfo->tm_sec = 59;
		char notBefore[16];
		char notAfter[16];
		strftime(notBefore, sizeof(notBefore), "%Y%m%d%H%M%S", timeInfo);
		timeInfo->tm_year += 500;
		strftime(notAfter, sizeof(notAfter), "%Y%m%d%H%M%S", timeInfo);
		stat = mbedtls_x509write_crt_set_validity(&ctx, notBefore, notAfter);
		check_quit(stat < 0, log, "Failed to set validity: {}", interpretMbedtlsError(stat));

		stat = mbedtls_x509write_crt_set_basic_constraints(&ctx, 1, 0);
		check_quit(stat < 0, log, "Failed to set basic constraints: {}", interpretMbedtlsError(stat));

		ByteBuffer der;
		der.resize(2048);
		stat = mbedtls_x509write_crt_der(&ctx, der.data(), der.size(), mbedtls_ctr_drbg_random, ctr_drbg);
		check_quit(stat < 0, log, "Failed to serialize X.509 certificate: {}", interpretMbedtlsError(stat));

		mbedtls_x509write_crt_free(&ctx);

		der.shiftTowardBegin(der.size() - stat);
		der.resize(stat);

		writeBinary("cert.der", der);
	}
}