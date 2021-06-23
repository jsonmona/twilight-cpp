#ifndef SERVER_PLATFORM_WINDOWS_ENCODER_SOFTWARE_H_
#define SERVER_PLATFORM_WINDOWS_ENCODER_SOFTWARE_H_


#include "DeviceManagerD3D.h"

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <array>


struct CaptureDataD3D;
struct EncoderData;
struct ExtraData;

class EncoderSoftware {
	LoggerPtr log;

	int width, height;
	int inputWidth, inputHeight;
	DXGI_FORMAT inputFormat;
	std::shared_ptr<DeviceManagerD3D> devs;
	std::function<CaptureDataD3D()> onFrameRequest;
	std::function<void(EncoderData*)> onDataAvailable;

	AVCodec* encoder = nullptr;
	AVCodecContext* encoderCtx = nullptr;
	SwsContext* swsCtx = nullptr;
	std::array<uint8_t*, 4> frameData = {};  // free first pointer only
	std::array<int, 4> frameLinesize;

	std::atomic_bool runFlag;
	std::thread runThread;

	void _run();
	ExtraData _fetchFrame(AVFrame* frame);

public:
	EncoderSoftware(const std::shared_ptr<DeviceManagerD3D>& _devs, int _width, int _height);
	~EncoderSoftware();

	inline void setFrameRequestCallback(const decltype(onFrameRequest)& x) { onFrameRequest = x; }
	inline void setDataAvailableCallback(const decltype(onDataAvailable)& x) { onDataAvailable = x; }

	void start();
	void stop();
};


#endif