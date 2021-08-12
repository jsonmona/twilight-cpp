#ifndef SERVER_PLATFORM_SOFTWARE_ENCODER_SOFTWARE_H_
#define SERVER_PLATFORM_SOFTWARE_ENCODER_SOFTWARE_H_


#include "common/log.h"
#include "common/ByteBuffer.h"
#include "common/ffmpeg-headers.h"

#include "common/platform/software/TextureSoftware.h"

#include "server/CaptureData.h"


class EncoderSoftware {
	LoggerPtr log;

	int width, height;
	std::function<CaptureData<TextureSoftware>()> onFrameRequest;
	std::function<void(CaptureData<ByteBuffer>&&)> onDataAvailable;

	AVCodec* encoder = nullptr;
	AVCodecContext* encoderCtx = nullptr;

	std::atomic_bool runFlag;
	std::thread runThread;

	void _run();

public:
	EncoderSoftware(int _width, int _height);
	~EncoderSoftware();

	inline void setOnFrameRequest(const decltype(onFrameRequest)& x) { onFrameRequest = x; }
	inline void setDataAvailableCallback(const decltype(onDataAvailable)& x) { onDataAvailable = x; }

	void start();
	void stop();
};


#endif