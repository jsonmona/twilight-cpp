#include "NetworkSocket.h"

#include "common/ByteBuffer.h"
#include "common/util.h"

#include <mbedtls/error.h>

#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/io/coded_stream.h>

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


template<typename Fn>
class SocketInputWrapper : public google::protobuf::io::CopyingInputStream {
public:
	SocketInputWrapper(mbedtls_ssl_context* ssl_, Fn onError_) :
		ssl(ssl_), onError(onError_) {}

	virtual ~SocketInputWrapper() {
	}

	virtual int Read(void* buf, int size) override {
		int stat = mbedtls_ssl_read(ssl, reinterpret_cast<uint8_t*>(buf), size);
		if (stat < 0) {
			onError(stat);
			return -1;
		}
		return stat;
	}

private:
	mbedtls_ssl_context* ssl;
	Fn onError;
};


NetworkSocket::NetworkSocket() :
	log(createNamedLogger("NetworkSocket")),
	connected(false),
	remoteCert(nullptr), localCert(nullptr), localPrivkey(nullptr)
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
	connected(true), ctx(initCtx),
	remoteCert(nullptr), localCert(nullptr), localPrivkey(nullptr)
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
	
	mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);

	if(remoteCert != nullptr)
		mbedtls_ssl_conf_ca_chain(&conf, remoteCert, nullptr);

	if(localCert != nullptr)
		mbedtls_ssl_conf_own_cert(&conf, localCert, localPrivkey);
	
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

bool NetworkSocket::verifyCert() {
	uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
	if (flags == 0)
		return true;

	log->info("SSL verification error: {:08x}", flags);
	return false;
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

bool NetworkSocket::send(const msg::Packet& pkt, const uint8_t* extraData) {
	std::lock_guard lock(sendLock);

	int ret;

	size_t packetLen = pkt.ByteSizeLong();
	size_t written = 0;
	sendBuffer.resize(packetLen + 16);

	const size_t extraDataLen = pkt.extra_data_len();

	/* Write data */ {
		google::protobuf::io::ArrayOutputStream aout(sendBuffer.data(), sendBuffer.size());
		google::protobuf::io::CodedOutputStream cout(&aout);

		cout.WriteVarint64(packetLen);
		if (sendBuffer.size() - cout.ByteCount() < packetLen) {
			log->critical("Packet length takes too much space: {} bytes used", cout.ByteCount());
			return false;
		}

		if (!pkt.SerializeToCodedStream(&cout)) {
			log->critical("Failed to serialize to coded stream");
			return false;
		}

		written = cout.ByteCount();
	}

	size_t offset = 0;
	while (offset < written) {
		ret = mbedtls_ssl_write(&ssl, sendBuffer.data() + offset, written - offset);
		if (ret < 0) {
			reportDisconnected(ret);
			return false;
		}

		offset += ret;
	}

	if (extraDataLen > 0) {
		check_quit(extraData == nullptr, log, "Extra data is nullptr (expected {} bytes)", extraDataLen);
		offset = 0;
		while (offset < extraDataLen) {
			ret = mbedtls_ssl_write(&ssl, extraData + offset, extraDataLen - offset);
			if (ret < 0) {
				reportDisconnected(ret);
				return false;
			}

			offset += ret;
		}
	}

	return true;
}

bool NetworkSocket::recv(msg::Packet* pkt, ByteBuffer* extraData) {
	std::lock_guard lock(recvLock);

	if (!connected.load(std::memory_order_acquire))
		return false;

	if (!inputStream) {
		auto* siw = new SocketInputWrapper(&ssl, [this](int errnum) { reportDisconnected(errnum); });
		zeroCopyInputStream = std::make_unique<google::protobuf::io::CopyingInputStreamAdaptor>(siw);
		inputStream = std::make_unique<google::protobuf::io::CodedInputStream>(zeroCopyInputStream.get());
	}

	auto limit = inputStream->ReadLengthAndPushLimit();

	if (!pkt->ParseFromCodedStream(inputStream.get()))
		return false;

	if (!inputStream->ConsumedEntireMessage())
		return false;

	inputStream->PopLimit(limit);

	int extraDataLen = pkt->extra_data_len();

	if (extraDataLen > 0) {
		if (extraData != nullptr) {
			extraData->resize(extraDataLen);

			if (!inputStream->ReadRaw(extraData->data(), extraDataLen))
				return false;
		}
		else {
			inputStream->Skip(extraDataLen);
		}
	}

	return true;
}

ByteBuffer NetworkSocket::getRemotePubkey() {
	ByteBuffer arr(8192);
	int ret;

	/* write pubkey */ {
		std::scoped_lock lock(sendLock, recvLock);
		const mbedtls_x509_crt* cert = mbedtls_ssl_get_peer_cert(&ssl);
		if (cert == nullptr)
			return ByteBuffer(0);

		mbedtls_pk_context* pk = const_cast<mbedtls_pk_context*>(&cert->pk);
		ret = mbedtls_pk_write_pubkey_der(pk, arr.data(), arr.size());
	}

	if (ret > 0) {
		arr.shiftTowardBegin(arr.size() - ret);
		arr.resize(ret);
	}
	else {
		log->warn("Failed to serialize remote pubkey: {}", interpretMbedtlsError(ret));
		arr.resize(0);
	}

	return arr;
}

ByteBuffer NetworkSocket::getRemoteCert() {
	ByteBuffer arr(8192);

	/* write pubkey */ {
		std::scoped_lock lock(sendLock, recvLock);
		const mbedtls_x509_crt* cert = mbedtls_ssl_get_peer_cert(&ssl);
		if (cert == nullptr)
			return ByteBuffer(0);

		arr.resize(cert->raw.len);
		memcpy(arr.data(), cert->raw.p, cert->raw.len);
	}

	return arr;
}

void NetworkSocket::reportDisconnected(int errnum) {
	char buf[2048];
	bool prev = connected.exchange(false, std::memory_order_acq_rel);

	if (prev) {
		mbedtls_net_free(&ctx);
		mbedtls_strerror(errnum, buf, 2048);
		if(onDisconnected)
			onDisconnected(buf);
	}
}