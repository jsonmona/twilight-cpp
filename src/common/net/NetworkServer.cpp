#include "NetworkServer.h"


NetworkServer::NetworkServer() :
	log(createNamedLogger("NetworkServer"))
{
	mbedtls_net_init(&ctx);
}

NetworkServer::~NetworkServer() {
	flagListen.store(false, std::memory_order_release);
	mbedtls_net_free(&ctx);

	if (listenThread.joinable())
		listenThread.join();
}

void NetworkServer::startListen(uint16_t port) {
	bool prev = flagListen.exchange(true, std::memory_order_acq_rel);
	check_quit(prev, log, "Starting listening again when already listening");

	char portString[8] = {};
	sprintf(portString, "%d", port);
	static_assert(sizeof(port) == 2, "sprintf above is safe because port is u16");

	//FIXME: Can socket be bound twice?
	int stat = mbedtls_net_bind(&ctx, "0.0.0.0", portString, MBEDTLS_NET_PROTO_TCP);
	check_quit(stat != 0, log, "Failed to bind socket");

	//FIXME: Is this really needed?
	if (listenThread.joinable())
		listenThread.join();

	listenThread = std::thread([this, port]() {
		while (flagListen.load(std::memory_order_acquire)) {
			mbedtls_net_context client;
			mbedtls_net_init(&client);
			int stat = mbedtls_net_accept(&ctx, &client, nullptr, 0, nullptr);
			if (stat != 0) {
				mbedtls_net_free(&client);
				break;
			}
			onNewConnection(std::make_unique<NetworkSocket>(client));
		}
	});
}

void NetworkServer::stopListen() {
	flagListen.store(false, std::memory_order_release);
	mbedtls_net_free(&ctx);

	if (listenThread.joinable())
		listenThread.join();
}