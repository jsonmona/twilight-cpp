#ifndef CLIENT_STREAM_CLIENT_H_
#define CLIENT_STREAM_CLIENT_H_


#include "common/log.h"
#include "common/NetworkInputStream.h"

#include <packet.pb.h>

#include <mbedtls/net_sockets.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <functional>

class StreamClient {
	LoggerPtr log;

	std::atomic_bool flagRun;
	std::thread recvThread;

    mbedtls_net_context net;
    NetworkInputStream input;

	std::function<void(const msg::Packet&, uint8_t*)> onNextPacket;

	void _recvRun();

public:
	StreamClient();
	~StreamClient();

	void setOnNextPacket(const decltype(onNextPacket)& fn) { onNextPacket = fn; }

	bool connect(const char* addr);
	void disconnect();
};


#endif