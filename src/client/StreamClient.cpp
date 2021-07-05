#include "StreamClient.h"

#include <packet.pb.h>


static const char* PORT = "6495";

static void debug_mbedtls(void* ctx, int level, const char* file, int line, const char* str) {
    spdlog::level::level_enum lv = spdlog::level::critical;
    if (1 <= level && level <= 5)
        lv = static_cast<spdlog::level::level_enum>(level);
    spdlog::log(lv, "MbedTLS debug log: {}:{:04d}: {}", file, line, str);
}

StreamClient::StreamClient() :
	log(createNamedLogger("StreamClient")), flagRun(false)
{
    int stat;

    mbedtls_net_init(&net);
}

StreamClient::~StreamClient() {
    mbedtls_net_free(&net);
}

bool StreamClient::connect(const char* addr) {
    int stat;

    flagRun.store(true, std::memory_order_release);

    stat = mbedtls_net_connect(&net, addr, PORT, MBEDTLS_NET_PROTO_TCP);
    if (stat < 0) {
        log->info("Socket connect failure: {} ({}:{})", stat, addr, PORT);
        return false;
    }
    
    input.init(&net);
    recvThread = std::thread([this]() { _recvRun(); });
    return true;
}

void StreamClient::disconnect() {
    flagRun.store(false, std::memory_order_release);
    recvThread.join();

    input.reset();
    mbedtls_net_close(&net);
}

void StreamClient::_recvRun() {
    int lastExtraDataLen = 0;
    uint8_t* extraData = nullptr;

    while (flagRun.load(std::memory_order_acquire)) {
        auto coded = input.coded();

        auto limit = coded->ReadLengthAndPushLimit();

        msg::Packet pkt;
        pkt.ParseFromCodedStream(coded.get());

        coded->PopLimit(limit);

        int extraDataLen = pkt.extra_data_len();
        if (extraDataLen) {
            if (lastExtraDataLen < extraDataLen) {
                extraData = reinterpret_cast<uint8_t*>(realloc(extraData, extraDataLen));
                lastExtraDataLen = extraDataLen;
            }

            coded->ReadRaw(extraData, extraDataLen);
        }

        onNextPacket(pkt, extraData);
    }
}
