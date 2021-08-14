#include "StreamClient.h"

#include <packet.pb.h>

constexpr uint16_t SERVICE_PORT = 6495;


StreamClient::StreamClient() :
	log(createNamedLogger("StreamClient"))
{
}

StreamClient::~StreamClient() {
}

void StreamClient::connect(const char* addr) {
    conn.connect(addr, SERVICE_PORT);
    recvThread = std::thread([this]() { _runRecv(); });
}

void StreamClient::disconnect() {
    conn.disconnect();
    recvThread.join();
}

void StreamClient::_runRecv() {
    bool stat;
    ByteBuffer extraData;
    msg::Packet pkt;

    while (true) {
        auto coded = conn.input().coded();
        if (!conn.isConnected())
            break;

        auto limit = coded->ReadLengthAndPushLimit();

        stat = pkt.ParseFromCodedStream(coded.get());
        if (!stat || !coded->ConsumedEntireMessage())
            break;

        coded->PopLimit(limit);

        int extraDataLen = pkt.extra_data_len();
        bool hasExtraData = (extraDataLen != 0);
        if (hasExtraData) {
            if(extraData.size() < extraDataLen)
                extraData.resize(extraDataLen);

            stat = coded->ReadRaw(extraData.data(), extraDataLen);
            if (!stat)
                break;
        }

        onNextPacket(pkt, extraData.data());
    }

    log->info("Stopping receive loop");
}
