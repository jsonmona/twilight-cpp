#include "NetworkOutputStream.h"

#include <mbedtls/net.h>

using google::protobuf::io::ZeroCopyOutputStream;
using google::protobuf::io::CodedOutputStream;


NetworkOutputStream::NetworkOutputStream() :
	log(createNamedLogger("NetworkOutputStream")),
	net(nullptr),
	buf(nullptr), totalBytes(0), dirtyBytes(0)
{
	buf = reinterpret_cast<uint8_t*>(malloc(BUF_SIZE));
}

NetworkOutputStream::NetworkOutputStream(NetworkOutputStream&& move) {
	log = move.log;
	net = move.net;

	buf = move.buf;
	totalBytes = move.totalBytes;
	dirtyBytes = move.dirtyBytes;

	move.net = nullptr;
	move.buf = nullptr;
}

NetworkOutputStream::~NetworkOutputStream() {
	free(buf);
}

void NetworkOutputStream::init(mbedtls_net_context* _net) {
	check_quit(net != nullptr, log, "Reinitialization without reset");

	net = _net;
}

void NetworkOutputStream::reset() {
	net = nullptr;
}

bool NetworkOutputStream::flush() {
	if (dirtyBytes <= 0)
		return true;

	int offset = 0;
	while (offset < dirtyBytes) {
		int ret = mbedtls_net_send(net, buf + offset, dirtyBytes - offset);
		if (ret < 0) {
			log->warn("Unable to write to socket");
			return false;
		}
		if (ret == 0)
			mbedtls_net_usleep(1);
		offset += ret;
	}

	dirtyBytes = 0;
	return true;
}

std::unique_ptr<CodedOutputStream> NetworkOutputStream::coded() {
	return std::make_unique<CodedOutputStream>(this);
}

bool NetworkOutputStream::Next(void** data, int* size) {
	//TODO: Somehow improve this (maybe using thread?)
	if (BUF_SIZE - dirtyBytes <= 64) {
		if (!flush())
			return false;
	}

	*data = buf + dirtyBytes;
	*size = BUF_SIZE - dirtyBytes;

	dirtyBytes = BUF_SIZE;
	totalBytes += *size;

	return true;
}

void NetworkOutputStream::BackUp(int count) {
	dirtyBytes -= count;
	totalBytes -= count;
}

int64_t NetworkOutputStream::ByteCount() const {
	return totalBytes;
}

bool NetworkOutputStream::WriteAliasedRaw(const void* data, int size) {
	return false;
}

bool NetworkOutputStream::AllowsAliasing() const {
	return false;
}
