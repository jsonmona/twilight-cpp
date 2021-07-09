#include "NetworkSocket.h"

#include <common/ByteBuffer.h>


NetworkSocket::NetworkSocket() :
	log(createNamedLogger("NetworkSocket")),
	nin(this), nout(this),
	sock(ioCtx)
{}

NetworkSocket::NetworkSocket(asio::ip::tcp::socket&& _sock) :
	log(createNamedLogger("NetworkSocket")),
	nin(this), nout(this),
	sock(std::move(_sock))
{}

NetworkSocket::~NetworkSocket() {
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

	log->info("Socket connected to {} ({})", addr, endpoint.address().to_string());
	recvThread = std::thread([this]() {
		ByteBuffer buffer(4096);
		asio::error_code err;
		auto buf = asio::buffer(buffer.data(), buffer.size());

		while (sock.is_open()) {
			size_t len = sock.read_some(buf, err);
			if (err)
				break;
			if(len > 0)
				nin.pushData(buffer.data(), len);
		}

		log->info("Socket disconnected due to error");
	});

	return true;
}
