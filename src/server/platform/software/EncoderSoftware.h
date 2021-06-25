#ifndef SERVER_PLATFORM_WINDOWS_ENCODER_SOFTWARE_H_
#define SERVER_PLATFORM_WINDOWS_ENCODER_SOFTWARE_H_


#include "TextureSoftware.h"

#include "common/log.h"
#include "server/CaptureData.h"

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}


class EncoderSoftware {
	LoggerPtr log;

	int width, height;
	std::function<CaptureData<TextureSoftware>()> onFrameRequest;
	std::function<void(CaptureData<std::vector<uint8_t>>&&)> onDataAvailable;

	AVCodec* encoder = nullptr;
	AVCodecContext* encoderCtx = nullptr;

	std::atomic_bool runFlag;
	std::thread runThread;

	void _run();

public:
	EncoderSoftware(int _width, int _height);
	~EncoderSoftware();

	inline void setFrameRequestCallback(const decltype(onFrameRequest)& x) { onFrameRequest = x; }
	inline void setDataAvailableCallback(const decltype(onDataAvailable)& x) { onDataAvailable = x; }

	void start();
	void stop();
};


#endif