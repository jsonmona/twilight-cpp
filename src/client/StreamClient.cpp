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

class SocketInputWrapper : public google::protobuf::io::ZeroCopyInputStream {
public:
    explicit SocketInputWrapper(NetworkSocket* sock_) : sock(sock_), offset(0) {}
    ~SocketInputWrapper() = default;

    bool Next(const void** data, int* size) override {
        if (buf.size() <= offset) {
            buf.resize(16384);
            if (!sock->recv(&buf))
                return false;
            offset = 0;
        }

        *data = buf.data() + offset;
        *size = buf.size() - offset;
        offset = buf.size();
        byteCount += *size;
        return true;
    }

    void BackUp(int count) override {
        offset -= count;
        byteCount -= count;
    }

    bool Skip(int count) override {
        int consumed = 0;
        const void* data;
        int size;
        while (consumed < count) {
            if (!Next(&data, &size))
                return false;
            consumed += size;
        }
        if (count < consumed)
            BackUp(consumed - count);

        return true;
    }

    int64_t ByteCount() const override {
        return byteCount;
    }

private:
    NetworkSocket* sock;
    size_t offset;
    int64_t byteCount;
    ByteBuffer buf;
};

void StreamClient::_runRecv() {
    bool stat;
    SocketInputWrapper zin(&conn);
    google::protobuf::io::CodedInputStream cin(&zin);
    ByteBuffer extraData(16384);
    msg::Packet pkt;

    while (true) {
        auto limit = cin.ReadLengthAndPushLimit();

        stat = pkt.ParseFromCodedStream(&cin);
        if (!stat || !cin.ConsumedEntireMessage())
            break;

        cin.PopLimit(limit);

        int extraDataLen = pkt.extra_data_len();
        bool hasExtraData = (extraDataLen != 0);
        if (hasExtraData) {
            if(extraData.capacity() < extraDataLen)
                extraData.reserve(extraDataLen);

            stat = cin.ReadRaw(extraData.data(), extraDataLen);
            if (!stat)
                break;
        }

        onNextPacket(pkt, extraData.data());
    }

    log->info("Stopping receive loop");
}
