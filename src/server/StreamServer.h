#ifndef SERVER_STREAM_MANAGER_H_
#define SERVER_STREAM_MANAGER_H_


#include "CapturePipeline.h"

#include "common/log.h"

#include <cstdio>
#include <memory>

class StreamServer {
	LoggerPtr log;
	FILE* f = nullptr;
	
	std::unique_ptr<CapturePipeline> capture;
	std::shared_ptr<CursorData> cursorData;

	void _processOutput(CaptureData<std::vector<uint8_t>>&& cap);
	void _writeOutput(const msg::Packet& pck, const uint8_t* extraData);

public:
	StreamServer();

	void start();
	void stop();
};


#endif