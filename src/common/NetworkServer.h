#ifndef COMMON_NETWORK_SERVER_H_
#define COMMON_NETWORK_SERVER_H_


#include "common/log.h"
#include "common/NetworkSocket.h"

#include <asio.hpp>

#include <atomic>
#include <functional>
#include <thread>


class NetworkServer {
	LoggerPtr log;

	std::atomic<bool> flagListen;
	std::thread listenThread;
	asio::io_context ioCtx;
	asio::ip::tcp::acceptor acceptor;

	std::function<void(std::unique_ptr<NetworkSocket>&&)> onNewConnection;

public:
	NetworkServer();
	NetworkServer(const NetworkServer& copy) = delete;
	NetworkServer(NetworkServer&& move) = delete;

	~NetworkServer();

	void startListen(uint16_t port);
	void stopListen();

	template<class Fn>
	void setOnNewConnection(Fn fn) { onNewConnection = fn; }
};


#endif