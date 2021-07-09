#include "NetworkInputStream.h"

#include "common/NetworkSocket.h"

using google::protobuf::io::ZeroCopyInputStream;
using google::protobuf::io::CodedInputStream;


NetworkInputStream::NetworkInputStream(NetworkSocket* _socket) :
    log(createNamedLogger("NetworkInputStream")), socket(_socket),
    totalBytes(0), acquiredSize(0)
{
}

NetworkInputStream::~NetworkInputStream() {
}

bool NetworkInputStream::Next(const void** data, int* size) {
    //TODO: Test if invalid

    ByteBuffer* now = nullptr;

    /* lock_guard */ {
        std::lock_guard<std::mutex> lock(bufferLock);
        if (!buf.empty()) {
            now = &buf.front();
            if (now->size() <= acquiredSize) {
                acquiredSize = 0;
                buf.pop_front();

                if (buf.empty())
                    now = nullptr;
                else
                    now = &buf.front();
            }
        }
    }

    if (now != nullptr) {
        *data = now->data() + acquiredSize;
        *size = now->size() - acquiredSize;
        acquiredSize = now->size();
    }
    else {
        *data = nullptr;
        *size = 0;
    }
    return true;
}

void NetworkInputStream::BackUp(int count) {
    acquiredSize -= count;
    totalBytes -= count;
}

bool NetworkInputStream::Skip(int count) {
    log->error("Skip is not implemented!");
    return false;
}

int64_t NetworkInputStream::ByteCount() const {
    return totalBytes;
}

void NetworkInputStream::pushData(const uint8_t* data, size_t len) {
    ByteBuffer now(len);
    memcpy(now.data(), data, len);
    
    std::lock_guard lock(bufferLock);
    buf.emplace_back(std::move(now));
}

std::unique_ptr<CodedInputStream> NetworkInputStream::coded() {
    return std::make_unique<CodedInputStream>(this);
}
