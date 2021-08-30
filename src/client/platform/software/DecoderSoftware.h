#ifndef CLIENT_PLATFORM_SOFTWARE_DECODER_SOFTWARE_H_
#define CLIENT_PLATFORM_SOFTWARE_DECODER_SOFTWARE_H_


#include "common/log.h"
#include "common/util.h"
#include "common/ffmpeg-headers.h"
#include "common/ByteBuffer.h"
#include "common/DesktopFrame.h"

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

	std::atomic<bool> flagRun;
	std::thread looper;

	std::mutex packetLock;
	std::condition_variable packetCV;
	std::deque<DesktopFrame<std::pair<uint8_t*, size_t>>> packetQueue;

	std::mutex frameLock;
	std::condition_variable frameCV;
	std::deque<DesktopFrame<TextureSoftware>> frameQueue;
	MinMaxTrackingRingBuffer<size_t, 32> frameBufferHistory;

	void _run();

public:
	DecoderSoftware();
	~DecoderSoftware();

	void start();
	void stop();

	void pushData(DesktopFrame<ByteBuffer>&& nextData);
	DesktopFrame<TextureSoftware> popData();
};


#endif