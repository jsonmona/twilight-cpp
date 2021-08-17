#ifndef SERVER_PLATFORM_SOFTWARE_ENCODER_SOFTWARE_H_
#define SERVER_PLATFORM_SOFTWARE_ENCODER_SOFTWARE_H_


#include "common/log.h"
#include "common/ByteBuffer.h"
#include "common/ffmpeg-headers.h"

#include "common/platform/software/TextureSoftware.h"

#include "server/CaptureData.h"

#include <deque>


class EncoderSoftware {
	LoggerPtr log;

	std::function<void(CaptureData<ByteBuffer>&&)> onDataAvailable;

	int width, height;
	AVCodec* encoder = nullptr;
	AVCodecContext* encoderCtx = nullptr;

	std::atomic<bool> flagRun;

	std::thread runThread;
	std::mutex dataLock;
	std::condition_variable dataCV;
	std::deque<CaptureData<TextureSoftware>> dataQueue;

	void _run();

public:
	EncoderSoftware(int _width, int _height);
	~EncoderSoftware();

	template<typename Fn>
	void setDataAvailableCallback(Fn fn) { onDataAvailable = std::move(fn); }

	void start();
	void stop();

	void pushData(CaptureData<TextureSoftware>&& newData);
};


#endif