#ifndef CLIENT_PLATFORM_SOFTWARE_DECODER_SOFTWARE_H_
#define CLIENT_PLATFORM_SOFTWARE_DECODER_SOFTWARE_H_


#include "common/log.h"
#include "common/ffmpeg-headers.h"

#include "common/platform/software/TextureSoftware.h"
#include "common/platform/software/ScaleSoftware.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <memory>
#include <thread>
#include <functional>


class DecoderSoftware {
	LoggerPtr log;

	AVCodec* codec;
	AVCodecContext* codecCtx;
	ScaleSoftware scale;

	std::atomic_bool flagRun;
	std::thread looper;

	std::function<void(TextureSoftware&&)> onFrameAvailable;

	struct EncodedData;
	std::mutex decoderQueueMutex;
	std::condition_variable decoderQueueCV;
	std::deque<std::unique_ptr<EncodedData>> decoderQueue;

	void _run();

public:
	DecoderSoftware();
	~DecoderSoftware();

	void setOnFrameAvailable(const decltype(onFrameAvailable)& fn) { onFrameAvailable = fn; }

	// thread safe. can be called from any thread
	void pushData(uint8_t* data, size_t len);

	void start();
	void stop();
};


#endif