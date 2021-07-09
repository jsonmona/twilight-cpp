#ifndef COMMON_NETWORK_OUTPUT_STREAM_H_
#define COMMON_NETWORK_OUTPUT_STREAM_H_


#include "common/log.h"
#include "common/ByteBuffer.h"

#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/coded_stream.h>

#include <memory>
#include <mutex>

class NetworkSocket;


class NetworkOutputStream : public google::protobuf::io::ZeroCopyOutputStream {
    static constexpr int BUF_SIZE = 4096;

    LoggerPtr log;
    NetworkSocket* socket;

    ByteBuffer buf;
    int64_t totalBytes;
    int dirtyBytes;

public:
    explicit NetworkOutputStream(NetworkSocket* _socket);
    ~NetworkOutputStream() override;
    NetworkOutputStream(const NetworkOutputStream& copy) = delete;
    NetworkOutputStream(NetworkOutputStream&& move);

    bool flush();

    std::unique_ptr<google::protobuf::io::CodedOutputStream> coded();

    bool Next(void** data, int* size) override;
    void BackUp(int count) override;
    int64_t ByteCount() const override;
    bool WriteAliasedRaw(const void* data, int size) override;
    bool AllowsAliasing() const override;
};


#endif