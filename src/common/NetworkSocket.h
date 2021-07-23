#ifndef COMMON_NETWORK_SOCKET_H_
#define COMMON_NETWORK_SOCKET_H_


#include "common/NetworkInputStream.h"
#include "common/NetworkOutputStream.h"

#include <asio.hpp>

#include <atomic>
#include <thread>
#include <functional>


class NetworkSocket {
	friend class NetworkInputStream;
	friend class NetworkOutputStream;

	LoggerPtr log;

	std::atomic<bool> connected;
	NetworkInputStream nin;
	NetworkOutputStream nout;

	std::thread recvThread;
	asio::io_context ioCtx;
	asio::ip::tcp::socket sock;

	std::function<void()> onDisconnected;

	void reportConnected();
	void reportDisconnected(const asio::error_code& err);

public:
	NetworkSocket();
	explicit NetworkSocket(asio::ip::tcp::socket&& _sock);
	NetworkSocket(const NetworkSocket& copy) = delete;
	NetworkSocket(NetworkSocket&& move) = delete;

	~NetworkSocket();

	bool connect(const char* addr, uint16_t port);
	bool isConnected() { return connected.load(std::memory_order_acquire); }

	NetworkInputStream& input() { return nin; }
	NetworkOutputStream& output() { return nout; }

	template<class Fn>
	void setOnDisconnected(Fn fn) { onDisconnected = fn; }
};


#endif