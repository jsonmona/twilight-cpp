#ifndef SERVER_PLATFORM_WINDOWS_ENCODER_D3D_H_
#define SERVER_PLATFORM_WINDOWS_ENCODER_D3D_H_


#include "common/log.h"
#include "common/ByteBuffer.h"

#include "common/platform/windows/DeviceManagerD3D.h"

#include "server/CaptureData.h"

#include <atomic>
#include <memory>
#include <vector>
#include <deque>
#include <functional>


class EncoderD3D {
	LoggerPtr log;

	int width, height;

	std::function<CaptureData<D3D11Texture2D>()> onFrameRequest;
	std::function<void(CaptureData<ByteBuffer>&&)> onDataAvailable;

	std::deque<CaptureData<long long>> extraData;

	MFDxgiDeviceManager mfDeviceManager;
	MFTransform encoder;
	MFMediaEventGenerator eventGen;
	DWORD inputStreamId, outputStreamId;

	void _init();
	void _run();

	void _pushEncoderTexture(const D3D11Texture2D& tex, long long sampleDur, long long sampleTime);
	ByteBuffer _popEncoderData(long long* sampleTime);

public:
	EncoderD3D(DeviceManagerD3D _devs, int _width, int _height);
	~EncoderD3D();

	template<typename Fn>
	void setOnFrameRequest(Fn fn) { onFrameRequest = std::move(fn); }

	template<typename Fn>
	void setOnDataAvailable(Fn fn) { onDataAvailable = std::move(fn); }

	void start();
	void stop();

	void poll();
};


#endif