#ifndef SERVER_CAPTURE_PIPELINE_H_
#define SERVER_CAPTURE_PIPELINE_H_


#include "server/CaptureData.h"

#include <packet.pb.h>

#include <cstdint>
#include <memory>
#include <vector>
#include <functional>

class CapturePipeline {
protected:
	std::function<void(CaptureData<std::vector<uint8_t>>&&)> writeOutput;

public:
	static std::unique_ptr<CapturePipeline> createInstance();

	CapturePipeline() = default;
	CapturePipeline(const CapturePipeline& copy) = delete;
	CapturePipeline(CapturePipeline&& move) = default;
	CapturePipeline& operator=(const CapturePipeline& copy) = delete;
	CapturePipeline& operator=(CapturePipeline&& move) = delete;

	virtual ~CapturePipeline() {};

	inline void setOutputCallback(const decltype(writeOutput)& f) { writeOutput = f; }

	virtual void start() = 0;
	virtual void stop() = 0;
};


#endif