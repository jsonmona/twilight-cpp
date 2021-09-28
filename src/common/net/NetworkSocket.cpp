#include "NetworkSocket.h"

#include "common/ByteBuffer.h"
#include "common/util.h"

#include <mbedtls/error.h>

#include <string>


//FIXME: Deduplicate from NetworkServer.cpp
// Mozilla Intermediate SSL but prefers chacha20 over AES
constexpr static std::string_view ALLOWED_CIPHERS = "\
TLS-ECDHE-ECDSA-WITH-CHACHA20-POLY1305-SHA256:\
TLS-ECDHE-RSA-WITH-CHACHA20-POLY1305-SHA256:\
TLS-ECDHE-ECDSA-WITH-AES-128-GCM-SHA256:\
TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256:\
TLS-ECDHE-ECDSA-WITH-AES-256-GCM-SHA384:\
TLS-ECDHE-RSA-WITH-AES-256-GCM-SHA384:\
TLS-DHE-RSA-WITH-CHACHA20-POLY1305-SHA256:\
TLS-DHE-RSA-WITH-AES-128-GCM-SHA256:\
TLS-DHE-RSA-WITH-AES-256-GCM-SHA384";


NetworkSocket::NetworkSocket() :
	log(createNamedLogger("NetworkSocket")),
	connected(false)
{
	mbedtls_net_init(&ctx);
	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctr_drbg);

	stringSplit(ALLOWED_CIPHERS, ':', [&](std::string_view slice) {
		std::string name(slice.begin(), slice.end());
		const mbedtls_ssl_ciphersuite_t* ptr = mbedtls_ssl_ciphersuite_from_string(name.c_str());
		if (ptr != nullptr)
			allowedCiphersuites.push_back(ptr->id);
		else
			log->warn("Unknown cipher suite name {}", name);
	});
	allowedCiphersuites.push_back(0); // Terminator
	allowedCiphersuites.shrink_to_fit();

	int stat = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, nullptr, 0);
	check_quit(stat < 0, log, "Failed to seed ctr_drbg: {}", interpretMbedtlsError(stat));

	stat = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
	check_quit(stat < 0, log, "Failed to set defaults for SSL: {}", interpretMbedtlsError(stat));

	mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
	mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
	mbedtls_ssl_conf_ciphersuites(&conf, allowedCiphersuites.data());
	mbedtls_ssl_conf_min_version(&conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
}

NetworkSocket::NetworkSocket(mbedtls_net_context initCtx, const mbedtls_ssl_config* ssl_conf) :
	log(createNamedLogger("NetworkSocket")),
	connected(true), ctx(initCtx)
{
	mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctr_drbg);

	int stat;
	stat = mbedtls_ssl_setup(&ssl, ssl_conf);
	if (stat != 0) {
		log->warn("Failed to setup SSL context: {}", interpretMbedtlsError(stat));
		connected.store(false, std::memory_order_relaxed);
	}

	mbedtls_ssl_set_bio(&ssl, &ctx, mbedtls_net_send, mbedtls_net_recv, nullptr);
	
	stat = mbedtls_ssl_handshake(&ssl);
	if (stat != 0) {
		log->warn("Failed to perform SSL handshake: {}", interpretMbedtlsError(stat));
		connected.store(false, std::memory_order_relaxed);
	}
}

NetworkSocket::~NetworkSocket() {
	bool prev = connected.exchange(false, std::memory_order_acq_rel);

	if (prev) {
		log->warn("Socket deconstructed while connected");
		disconnect();
	}

	mbedtls_ssl_free(&ssl);
	mbedtls_net_free(&ctx);
	mbedtls_ctr_drbg_free(&ctr_drbg);
	mbedtls_entropy_free(&entropy);
	mbedtls_ssl_config_free(&conf);
}

bool NetworkSocket::connect(const char* addr, uint16_t port) {
	int stat;
	
	//TODO: Insert own cert and trusted CA here
	
	char portString[8] = {};
	sprintf(portString, "%d", (int) port);
	static_assert(sizeof(port) == 2, "sprintf above does not overflow because port is u16");

	stat = mbedtls_net_connect(&ctx, addr, portString, MBEDTLS_NET_PROTO_TCP);
	if (stat != 0)
		return false; //FIXME: Memory leak (this and other check_quit's)

	stat = mbedtls_ssl_setup(&ssl, &conf);
	check_quit(stat < 0, log, "Failed to setup SSL context: {}", interpretMbedtlsError(stat));

	mbedtls_ssl_set_bio(&ssl, &ctx, mbedtls_net_send, mbedtls_net_recv, nullptr);

	stat = mbedtls_ssl_handshake(&ssl);
	check_quit(stat < 0, log, "Failed to perform SSL handshake: {}", interpretMbedtlsError(stat));

	connected.store(true, std::memory_order_release);
	log->info("Connected to tls:{}:{}", addr, port);

	return true;
}

void NetworkSocket::disconnect() {
	bool prev = connected.exchange(false, std::memory_order_acq_rel);

	if (prev) {
		mbedtls_ssl_close_notify(&ssl);
		mbedtls_net_free(&ctx);
		if(onDisconnected)
			onDisconnected("");
	}
}

bool NetworkSocket::send(const void* data, size_t len) {
	int stat;
	if (len <= 0)
		return true;

	std::lock_guard lock(sendLock);

	const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);

	size_t written = 0;
	while (written < len) {
		stat = mbedtls_ssl_write(&ssl, ptr + written, len - written);
		if (stat < 0) {
			reportDisconnected(stat);
			return false;
		}
		written += stat;
	}
	return true;
}

bool NetworkSocket::send(const ByteBuffer& buf) {
	return send(buf.data(), buf.size());
}

bool NetworkSocket::recvExact(void* data, size_t len) {
	int stat;
	size_t readLen = 0;

	if (len <= 0)
		return true;

	uint8_t* ptr = reinterpret_cast<uint8_t*>(data);

	std::lock_guard lock(recvLock);
	while (readLen < len) {
		stat = mbedtls_ssl_read(&ssl, ptr + readLen, len - readLen);
		if (stat < 0) {
			reportDisconnected(stat);
			return false;
		}
		readLen += stat;
	}

	return true;
}

bool NetworkSocket::recvExact(ByteBuffer* buf) {
	return recv(buf->data(), buf->size());
}

int64_t NetworkSocket::recv(void* data, int64_t len) {
	int stat;

	if (len <= 0)
		return true;

	uint8_t* ptr = reinterpret_cast<uint8_t*>(data);

	std::lock_guard lock(recvLock);
	stat = mbedtls_ssl_read(&ssl, ptr, len);
	if (stat < 0) {
		reportDisconnected(stat);
		return stat;
	}

	return stat;
}

bool NetworkSocket::recv(ByteBuffer* buf) {
	int64_t read = recv(buf->data(), buf->size());
	if (read < 0)
		return false;
	buf->resize(read);
	return true;
}

void NetworkSocket::reportDisconnected(int errnum) {
	char buf[2048];
	bool prev = connected.exchange(false, std::memory_order_acq_rel);

	if (prev) {
		mbedtls_net_free(&ctx);
		mbedtls_strerror(errnum, buf, 2048);
		onDisconnected(buf);
	}
}