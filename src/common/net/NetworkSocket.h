#ifndef COMMON_NETWORK_SOCKET_H_
#define COMMON_NETWORK_SOCKET_H_


#include "common/log.h"
#include "common/ByteBuffer.h"

#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

#include <packet.pb.h>

#include <atomic>
#include <thread>
#include <string>
#include <functional>


class NetworkSocket {
public:
	NetworkSocket();
	NetworkSocket(mbedtls_net_context initCtx, const mbedtls_ssl_config* ssl_conf);
	NetworkSocket(const NetworkSocket& copy) = delete;
	NetworkSocket(NetworkSocket&& move) = delete;

	~NetworkSocket();

	bool connect(const char* addr, uint16_t port);
	bool isConnected() const { return connected.load(std::memory_order_relaxed); }

	void disconnect();

	template<class Fn>
	void setOnDisconnected(Fn fn) { onDisconnected = std::move(fn); }

	bool send(const msg::Packet& pkt, const uint8_t* extraData);
	bool send(const msg::Packet& pkt, const ByteBuffer& extraData) { return send(pkt, extraData.data()); }

	bool recv(msg::Packet* pkt, ByteBuffer* extraData);

private:
	LoggerPtr log;

	mbedtls_net_context ctx;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;

	ByteBuffer sendBuffer;
	std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> zeroCopyInputStream;
	std::unique_ptr<google::protobuf::io::CodedInputStream> inputStream;
	
	std::vector<int> allowedCiphersuites;

	std::atomic<bool> connected;
	std::mutex sendLock;
	std::mutex recvLock;

	std::function<void(std::string_view msg)> onDisconnected;

	void reportDisconnected(int errnum);
};


#endif