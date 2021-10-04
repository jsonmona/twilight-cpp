#include "StreamClient.h"

#include <packet.pb.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>


constexpr uint16_t SERVICE_PORT = 6495;


StreamClient::StreamClient() :
	log(createNamedLogger("StreamClient"))
{
    conn.setOnDisconnected([this](std::string_view msg) {
        onStateChange(State::DISCONNECTED, msg);
    });
}

StreamClient::~StreamClient() {
}

void StreamClient::connect(const char* addr_) {
    recvThread = std::thread([this, addr = std::string(addr_)]() {
        if (conn.connect(addr.c_str(), SERVICE_PORT)) {
            onStateChange(State::CONNECTED, "");
            _runRecv();
        }
        else
            onStateChange(State::DISCONNECTED, "Unable to connect"); //TODO: Pass message from OS
    });
}

void StreamClient::disconnect() {
    conn.disconnect();
    recvThread.join();
}

void StreamClient::_runRecv() {
    bool stat;
    msg::Packet pkt;
    ByteBuffer extraData;

    while (true) {
        stat = conn.recv(&pkt, &extraData);
        if (!stat)
            break;

        onNextPacket(pkt, extraData.data());
    }

    log->info("Stopping receive loop");
}
