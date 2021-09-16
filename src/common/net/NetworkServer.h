#ifndef COMMON_NETWORK_SERVER_H_
#define COMMON_NETWORK_SERVER_H_


#include "common/log.h"

#include "common/net/NetworkSocket.h"

#include <mbedtls/net_sockets.h>

#include <atomic>
#include <functional>
#include <thread>


class NetworkServer {
public:
	NetworkServer();
	NetworkServer(const NetworkServer& copy) = delete;
	NetworkServer(NetworkServer&& move) = delete;

	~NetworkServer();

	void startListen(uint16_t port);
	void stopListen();

	template<class Fn>
	void setOnNewConnection(Fn fn) { onNewConnection = std::move(fn); }

private:
	LoggerPtr log;

	std::atomic<bool> flagListen;
	std::thread listenThread;

	mbedtls_net_context ctx;

	std::function<void(std::unique_ptr<NetworkSocket>&&)> onNewConnection;
};


#endif