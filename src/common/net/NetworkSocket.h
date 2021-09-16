#ifndef COMMON_NETWORK_SOCKET_H_
#define COMMON_NETWORK_SOCKET_H_


#include "common/log.h"
#include "common/ByteBuffer.h"

#include <mbedtls/net_sockets.h>

#include <atomic>
#include <thread>
#include <string>
#include <functional>


class NetworkSocket {
public:
	NetworkSocket();
	explicit NetworkSocket(mbedtls_net_context initCtx);
	NetworkSocket(const NetworkSocket& copy) = delete;
	NetworkSocket(NetworkSocket&& move) = delete;

	~NetworkSocket();

	bool connect(const char* addr, uint16_t port);
	bool isConnected() const { return connected.load(std::memory_order_relaxed); }

	void disconnect();

	template<class Fn>
	void setOnDisconnected(Fn fn) { onDisconnected = std::move(fn); }

	bool send(const void* data, size_t len);
	bool send(const ByteBuffer& buf);

	bool recvAll(void* data, size_t len);
	bool recvAll(ByteBuffer* buf);

	int64_t recv(void* data, int64_t len);
	bool recv(ByteBuffer* buf);

private:
	LoggerPtr log;

	std::atomic<bool> connected;

	mbedtls_net_context ctx;

	std::mutex sendLock;
	std::mutex recvLock;

	std::function<void(std::string_view msg)> onDisconnected;

	void reportDisconnected(int errnum);
};


#endif