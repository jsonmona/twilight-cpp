#include "NetworkSocket.h"

#include <mbedtls/error.h>

#include <common/ByteBuffer.h>


NetworkSocket::NetworkSocket() :
	log(createNamedLogger("NetworkSocket")),
	connected(false)
{
	mbedtls_net_init(&ctx);
}

NetworkSocket::NetworkSocket(mbedtls_net_context initCtx) :
	log(createNamedLogger("NetworkSocket")),
	connected(true), ctx(initCtx)
{}

NetworkSocket::~NetworkSocket() {
	bool prev = connected.exchange(false, std::memory_order_acq_rel);

	if (prev) {
		log->warn("Socket deconstructed while connected");
		disconnect();
	}
}

bool NetworkSocket::connect(const char* addr, uint16_t port) {
	int stat;
	
	char portString[8] = {};
	sprintf(portString, "%d", (int) port);
	static_assert(sizeof(port) == 2, "sprintf above does not overflow because port is u16");

	stat = mbedtls_net_connect(&ctx, addr, portString, MBEDTLS_NET_PROTO_TCP);
	if (stat != 0)
		return false;

	log->info("Connected to tcp:{}:{}", addr, port);
	connected.store(true, std::memory_order_release);

	return true;
}

void NetworkSocket::disconnect() {
	bool prev = connected.exchange(false, std::memory_order_acq_rel);

	if (prev) {
		mbedtls_net_free(&ctx);
		if(onDisconnected)
			onDisconnected("");
	}
}

bool NetworkSocket::send(const void* data, size_t len) {
	int stat;
	if (len <= 0)
		return true;

	std::lock_guard lock(sendLock);

	const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);

	size_t written = 0;
	while (written < len) {
		stat = mbedtls_net_send(&ctx, ptr + written, len - written);
		if (stat < 0) {
			reportDisconnected(stat);
			return false;
		}
		written += stat;
	}
	return true;
}

bool NetworkSocket::send(const ByteBuffer& buf) {
	return send(buf.data(), buf.size());
}

bool NetworkSocket::recvExact(void* data, size_t len) {
	int stat;
	size_t readLen = 0;

	if (len <= 0)
		return true;

	uint8_t* ptr = reinterpret_cast<uint8_t*>(data);

	std::lock_guard lock(recvLock);
	while (readLen < len) {
		stat = mbedtls_net_recv(&ctx, ptr + readLen, len - readLen);
		if (stat < 0) {
			reportDisconnected(stat);
			return false;
		}
		readLen += stat;
	}

	return true;
}

bool NetworkSocket::recvExact(ByteBuffer* buf) {
	return recv(buf->data(), buf->size());
}

int64_t NetworkSocket::recv(void* data, int64_t len) {
	int stat;

	if (len <= 0)
		return true;

	uint8_t* ptr = reinterpret_cast<uint8_t*>(data);

	std::lock_guard lock(recvLock);
	stat = mbedtls_net_recv(&ctx, ptr, len);
	if (stat < 0) {
		reportDisconnected(stat);
		return stat;
	}

	return stat;
}

bool NetworkSocket::recv(ByteBuffer* buf) {
	int64_t read = recv(buf->data(), buf->size());
	if (read < 0)
		return false;
	buf->resize(read);
	return true;
}

void NetworkSocket::reportDisconnected(int errnum) {
	char buf[2048];
	bool prev = connected.exchange(false, std::memory_order_acq_rel);

	if (prev) {
		mbedtls_net_free(&ctx);
		mbedtls_strerror(errnum, buf, 2048);
		onDisconnected(buf);
	}
}