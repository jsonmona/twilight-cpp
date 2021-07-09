#include "NetworkOutputStream.h"

#include "common/NetworkSocket.h"


using google::protobuf::io::ZeroCopyOutputStream;
using google::protobuf::io::CodedOutputStream;


NetworkOutputStream::NetworkOutputStream(NetworkSocket* _socket) :
	log(createNamedLogger("NetworkOutputStream")),
	socket(_socket), buf(BUF_SIZE), totalBytes(0), dirtyBytes(0)
{
}

NetworkOutputStream::NetworkOutputStream(NetworkOutputStream&& move) :
	log(move.log),
	socket(nullptr), buf(BUF_SIZE), totalBytes(0), dirtyBytes(0)
{
	std::swap(socket, move.socket);
	std::swap(buf, move.buf);
	std::swap(totalBytes, move.totalBytes);
	std::swap(dirtyBytes, move.dirtyBytes);
}

NetworkOutputStream::~NetworkOutputStream() {
}

bool NetworkOutputStream::flush() {
	if (dirtyBytes == 0)
		return true;

	asio::error_code err;
	int offset = 0;
	while (offset < dirtyBytes) {
		auto buffer = asio::buffer(buf.data_char() + offset, dirtyBytes - offset);
		int64_t ret = socket->sock.write_some(buffer, err);
		if (err || ret < 0)
			return false;
		offset += ret;
	}

	dirtyBytes = 0;
	return true;
}

std::unique_ptr<CodedOutputStream> NetworkOutputStream::coded() {
	return std::make_unique<CodedOutputStream>(this);
}

bool NetworkOutputStream::Next(void** data, int* size) {
	//TODO: Test if invalid

	//TODO: Somehow improve this (maybe using thread?)
	if (BUF_SIZE - dirtyBytes <= 64) {
		if (!flush())
			return false;
	}

	*data = buf.data() + dirtyBytes;
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
	//TODO: Test if invalid

	const char* ptr = reinterpret_cast<const char*>(data);
	int offset = 0;
	while (offset < size) {
		auto buffer = asio::buffer(ptr + offset, size - offset);
		int64_t ret = socket->sock.send(buffer);
		if (ret < 0)
			return false;
		offset += ret;
	}
	return true;
}

bool NetworkOutputStream::AllowsAliasing() const {
	return true;
}
