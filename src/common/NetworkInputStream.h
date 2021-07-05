#ifndef COMMON_NETWORK_INPUT_STREAM_H_
#define COMMON_NETWORK_INPUT_STREAM_H_


#include "common/log.h"

#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/coded_stream.h>

#include <memory>

struct mbedtls_net_context;

class NetworkInputStream : public google::protobuf::io::ZeroCopyInputStream {
    static constexpr int BUF_SIZE = 16384 - 64;

    LoggerPtr log;
    mbedtls_net_context* net;

    uint8_t* buf;
    int64_t totalBytes;
    int readSize;
    int offset;

protected:
    bool Next(const void** data, int* size) override;
    void BackUp(int count) override;
    bool Skip(int count) override;
    int64_t ByteCount() const override;

public:
    NetworkInputStream();
    ~NetworkInputStream() override;

    void init(mbedtls_net_context* _net);
    void reset();

    std::unique_ptr<google::protobuf::io::CodedInputStream> coded();
};


#endif