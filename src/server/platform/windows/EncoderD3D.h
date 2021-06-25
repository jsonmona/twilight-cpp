#ifndef SERVER_PLATFORM_WINDOWS_ENCODER_D3D_H_
#define SERVER_PLATFORM_WINDOWS_ENCODER_D3D_H_


#include "common/log.h"

#include "server/CaptureData.h"
#include "DeviceManagerD3D.h"

#include <atomic>
#include <memory>
#include <vector>
#include <functional>


class EncoderD3D {
	LoggerPtr log;

	int width, height;
	std::shared_ptr<DeviceManagerD3D> devs;
	std::function<CaptureData<D3D11Texture2D>()> onFrameRequest;
	std::function<void(CaptureData<std::vector<uint8_t>>&&)> onDataAvailable;

	std::thread runThread;

	MFTransform encoder;
	DWORD inputStreamId, outputStreamId;

	void _init();
	void _run();

	void _pushEncoderTexture(const D3D11Texture2D& tex, long long sampleDur, long long sampleTime);
	std::vector<uint8_t> _popEncoderData(long long* sampleTime);

public:
	EncoderD3D(const std::shared_ptr<DeviceManagerD3D>& _devs, int _width, int _height);
	~EncoderD3D();

	inline void setFrameRequestCallback(const decltype(onFrameRequest)& x) { onFrameRequest = x; }
	inline void setDataAvailableCallback(const decltype(onDataAvailable)& x) { onDataAvailable = x; }

	void start();
	void stop();
};


#endif