#ifndef CLIENT_STREAM_CLIENT_H_
#define CLIENT_STREAM_CLIENT_H_


#include "common/log.h"

#include "common/net/NetworkSocket.h"

#include <packet.pb.h>

#include <atomic>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <functional>


class StreamClient {
public:
	enum class State : int {
		CONNECTED,
		DISCONNECTED
	};

	StreamClient();
	~StreamClient();

	template<typename Fn>
	void setOnNextPacket(Fn fn) { onNextPacket = std::move(fn); }

	template<typename Fn>
	void setOnStateChange(Fn fn) { onStateChange = std::move(fn); }

	void connect(const char* addr);
	void disconnect();

private:
	LoggerPtr log;

	NetworkSocket conn;

	std::function<void(const msg::Packet&, uint8_t*)> onNextPacket;
	std::function<void(State, std::string_view msg)> onStateChange;

	std::thread recvThread;
	void _runRecv();
};


#endif