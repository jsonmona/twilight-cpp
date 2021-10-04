#ifndef SERVER_STREAM_MANAGER_H_
#define SERVER_STREAM_MANAGER_H_


#include "CapturePipeline.h"

#include "common/log.h"

#include "common/net/NetworkServer.h"

#include "server/AudioEncoder.h"

#include <cstdio>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>


class StreamServer {
	LoggerPtr log;

	NetworkServer server;
	std::unique_ptr<NetworkSocket> conn;

	AudioEncoder audioEncoder;
	
	std::unique_ptr<CapturePipeline> capture;
	std::shared_ptr<CursorPos> cursorPos;

	std::chrono::steady_clock::time_point lastStatReport;

	void _runListen();

	void _processOutput(DesktopFrame<ByteBuffer>&& cap);

public:
	StreamServer();
	~StreamServer();

	void start();
	void stop();
};


#endif