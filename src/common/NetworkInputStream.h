#ifndef COMMON_NETWORK_INPUT_STREAM_H_
#define COMMON_NETWORK_INPUT_STREAM_H_


#include "common/log.h"
#include "common/ByteBuffer.h"

#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/coded_stream.h>

#include <memory>
#include <deque>

class NetworkSocket;


class NetworkInputStream : public google::protobuf::io::ZeroCopyInputStream {
    static constexpr int BUF_SIZE = 4096;

    LoggerPtr log;
    NetworkSocket* socket;

    std::mutex bufferLock;
    std::deque<ByteBuffer> buf;
    int acquiredSize;
    int64_t totalBytes;

protected:
    bool Next(const void** data, int* size) override;
    void BackUp(int count) override;
    bool Skip(int count) override;
    int64_t ByteCount() const override;

public:
    explicit NetworkInputStream(NetworkSocket* _socket);
    NetworkInputStream(const NetworkInputStream& copy) = delete;
    NetworkInputStream(NetworkInputStream&& move) = delete;
    ~NetworkInputStream() override;

    void pushData(const uint8_t* data, size_t len);

    std::unique_ptr<google::protobuf::io::CodedInputStream> coded();
};


#endif