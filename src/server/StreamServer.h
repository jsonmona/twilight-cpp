#ifndef SERVER_STREAM_MANAGER_H_
#define SERVER_STREAM_MANAGER_H_


#include "CapturePipeline.h"

#include "common/log.h"
#include "common/NetworkServer.h"

#include <cstdio>
#include <memory>
#include <thread>
#include <atomic>


class StreamServer {
	LoggerPtr log;

	NetworkServer server;
	std::unique_ptr<NetworkSocket> conn;
	
	std::unique_ptr<CapturePipeline> capture;
	std::shared_ptr<CursorData> cursorData;

	void _runListen();

	void _processOutput(CaptureData<ByteBuffer>&& cap);
	void _writeOutput(const msg::Packet& pck, const uint8_t* extraData);

public:
	StreamServer();
	~StreamServer();

	void start();
	void stop();
};


#endif