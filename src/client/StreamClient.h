#ifndef CLIENT_STREAM_CLIENT_H_
#define CLIENT_STREAM_CLIENT_H_


#include "common/log.h"
#include "common/NetworkSocket.h"

#include <packet.pb.h>

#include <atomic>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <functional>


class StreamClient {
	LoggerPtr log;

	NetworkSocket conn;

	std::function<void(const msg::Packet&, uint8_t*)> onNextPacket;

	std::thread recvThread;
	void _runRecv();

public:
	StreamClient();
	~StreamClient();

	void setOnNextPacket(const decltype(onNextPacket)& fn) { onNextPacket = fn; }

	void connect(const char* addr);
	void disconnect();
};


#endif