#ifndef COMMON_NETWORK_OUTPUT_STREAM_H_
#define COMMON_NETWORK_OUTPUT_STERAM_H_


#include "common/log.h"

#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/coded_stream.h>

#include <memory>

struct mbedtls_net_context;

class NetworkOutputStream : public google::protobuf::io::ZeroCopyOutputStream {
    static constexpr int BUF_SIZE = 16384 - 64;

    LoggerPtr log;
    mbedtls_net_context* net;

    uint8_t* buf;
    int64_t totalBytes;
    int dirtyBytes;

public:
    NetworkOutputStream();
    ~NetworkOutputStream() override;
    NetworkOutputStream(const NetworkOutputStream& copy) = delete;
    NetworkOutputStream(NetworkOutputStream&& move);

    void init(mbedtls_net_context* _net);
    void reset();
    bool flush();

    std::unique_ptr<google::protobuf::io::CodedOutputStream> coded();

    bool Next(void** data, int* size) override;
    void BackUp(int count) override;
    int64_t ByteCount() const override;
    bool WriteAliasedRaw(const void* data, int size) override;
    bool AllowsAliasing() const override;
};


#endif