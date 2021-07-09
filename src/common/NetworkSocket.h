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

	NetworkInputStream nin;
	NetworkOutputStream nout;

	std::thread recvThread;
	asio::io_context ioCtx;
	asio::ip::tcp::socket sock;

public:
	NetworkSocket();
	explicit NetworkSocket(asio::ip::tcp::socket&& _sock);
	NetworkSocket(const NetworkSocket& copy) = delete;
	NetworkSocket(NetworkSocket&& move) = delete;

	~NetworkSocket();

	bool connect(const char* addr, uint16_t port);

	NetworkInputStream& input() { return nin; }
	NetworkOutputStream& output() { return nout; }
};


#endif