#ifndef SERVER_STREAM_MANAGER_H_
#define SERVER_STREAM_MANAGER_H_


#include "CapturePipeline.h"

#include "common/log.h"
#include "common/NetworkOutputStream.h"

#include <mbedtls/net.h>

#include <cstdio>
#include <memory>
#include <thread>
#include <atomic>

class StreamServer {
	LoggerPtr log;

	mbedtls_net_context serverSock, sock;
	NetworkOutputStream output;

	std::atomic_bool flagRun;
	std::thread listenThread;
	
	std::unique_ptr<CapturePipeline> capture;
	std::shared_ptr<CursorData> cursorData;

	void _runListen();

	void _processOutput(CaptureData<std::vector<uint8_t>>&& cap);
	void _writeOutput(const msg::Packet& pck, const uint8_t* extraData);

public:
	StreamServer();
	~StreamServer();

	void start();
	void stop();
};


#endif