#include "NetworkInputStream.h"

#include <mbedtls/net.h>

using google::protobuf::io::ZeroCopyInputStream;
using google::protobuf::io::CodedInputStream;


NetworkInputStream::NetworkInputStream() :
    log(createNamedLogger("NetworkInputStream")), net(nullptr), buf(nullptr),
    totalBytes(0), readSize(0), offset(0)
{
    buf = reinterpret_cast<uint8_t*>(malloc(BUF_SIZE));
    check_quit(buf == nullptr, log, "Failed to allocate buffer");
}

NetworkInputStream::~NetworkInputStream() {
    free(buf);
}

bool NetworkInputStream::Next(const void** data, int* size) {
    while (readSize <= offset) {
        offset -= readSize;
        readSize = mbedtls_net_recv(net, buf, BUF_SIZE);
        if (readSize < 0) {
            log->warn("Error while recv: {}", readSize);
            return false;
        }
        if (readSize == 0)
            mbedtls_net_usleep(1);
    }
    *data = buf + offset;
    *size = readSize - offset;
    offset = readSize;
    totalBytes += readSize;
    return true;
}

void NetworkInputStream::BackUp(int count) {
    offset -= count;
    totalBytes -= count;
}

bool NetworkInputStream::Skip(int count) {
    offset += count;
    totalBytes += count;
    return true;
}

int64_t NetworkInputStream::ByteCount() const {
    return totalBytes;
}

void NetworkInputStream::init(mbedtls_net_context* _net) {
    check_quit(net != nullptr && net != _net, log, "Initialized again without being reset");
    net = _net;
}

void NetworkInputStream::reset() {
    net = nullptr;
}

std::unique_ptr<CodedInputStream> NetworkInputStream::coded() {
    return std::make_unique<CodedInputStream>(this);
}
