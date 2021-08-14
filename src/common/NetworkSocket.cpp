#include "NetworkSocket.h"

#include <common/ByteBuffer.h>


NetworkSocket::NetworkSocket() :
	log(createNamedLogger("NetworkSocket")),
	nin(this), nout(this), connected(false),
	sock(ioCtx)
{}

NetworkSocket::NetworkSocket(asio::ip::tcp::socket&& _sock) :
	log(createNamedLogger("NetworkSocket")),
	nin(this), nout(this), connected(true),
	sock(std::move(_sock))
{
	reportConnected();
}

NetworkSocket::~NetworkSocket() {
	if (isConnected()) {
		log->warn("Socket deconstructed while connected");
		disconnect();
	}

	// The thread probably is ended but not joined
	if (recvThread.joinable())
		recvThread.join();
}

bool NetworkSocket::connect(const char* addr, uint16_t port) {
	asio::error_code err;

	asio::ip::tcp::resolver resolver(ioCtx);
	asio::ip::tcp::resolver::query query(addr, "");

	asio::ip::tcp::resolver::iterator end;
	auto itr = resolver.resolve(query, err);
	if (err || itr == end)
		return false;

	asio::ip::tcp::endpoint endpoint = itr->endpoint();
	endpoint.port(port);

	sock.connect(endpoint, err);
	if (err)
		return false;

	reportConnected();

	return true;
}

void NetworkSocket::disconnect() {
	asio::error_code err;
	sock.close(err);
	if (err)
		log->warn("Error while disconnecting socket: {}", err.message());
	recvThread.join();
}

void NetworkSocket::reportConnected() {
	asio::error_code err;

	asio::ip::tcp::endpoint endpoint = sock.remote_endpoint(err);
	check_quit(!!err, log, "Failed to query remote endpoint");

	log->info("Socket connected to {}:{}", endpoint.address().to_string(), endpoint.port());
	connected.store(true, std::memory_order_release);

	recvThread = std::thread([this]() {
		ByteBuffer buffer(4096);
		asio::error_code err;
		auto buf = asio::buffer(buffer.data(), buffer.size());

		while (isConnected()) {
			size_t len = sock.read_some(buf, err);
			if (err) {
				reportDisconnected(err);
				break;
			}
			if (len > 0)
				nin.pushData(buffer.data(), len);
		}
		onDisconnected();
	});
}

void NetworkSocket::reportDisconnected(const asio::error_code& err) {
	bool prev = connected.exchange(false, std::memory_order_acq_rel);

	if (prev) {
		log->info("Socket disconnected: {}", err.message());
	}
}