#include "NetworkServer.h"


NetworkServer::NetworkServer() :
	log(createNamedLogger("NetworkServer")),
	acceptor(ioCtx)
{
}

NetworkServer::~NetworkServer() {
}

void NetworkServer::startListen(uint16_t port) {
	check_quit(listenThread.joinable(), log, "Tried to start listening while running");

	asio::error_code err;
	acceptor.open(asio::ip::tcp::v4());

	asio::ip::tcp::endpoint endpoint(asio::ip::address_v4::any(), port);
	acceptor.bind(endpoint, err);

	check_quit(!!err, log, "Unable to bind socket");

	acceptor.listen();

	flagListen.store(true, std::memory_order_release);

	listenThread = std::thread([this]() {
		while (flagListen.load(std::memory_order_acquire)) {
			asio::error_code err;
			asio::ip::tcp::socket sock = acceptor.accept(err);
			if (err)
				break;
			onNewConnection(std::make_unique<NetworkSocket>(std::move(sock)));
		}
	});
}

void NetworkServer::stopListen() {
	asio::error_code err;
	acceptor.close(err);
	if (err)
		log->warn("Error while closeing acceptor: {}", err.message());

	flagListen.store(false, std::memory_order_release);
	listenThread.join();
}