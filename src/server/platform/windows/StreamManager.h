#ifndef STREAM_MANAGER_H_
#define STREAM_MANAGER_H_

#include "common/platform/windows/ComWrapper.h"
#include "ColorConvD3D.h"

#include <atomic>
#include <vector>
#include <thread>
#include <cstdint>
#include <mutex>
#include <mutex>
#include <functional>


struct VideoFrame {
	//long long frameId;
	//bool hasDRMMasked;
	//int cursorX, cursorY;
	//int cursorHotspotX, cursorHotspotY;
	//int cursorHeight, cursorWidth;
	std::vector<uint8_t> desktopImage;
	//std::vector<uint8_t> cursorImage;
};


//TODO: Use subclass to support win8.1 (dxgifactory1)
class StreamManager {
	// Valid after constructor
	DxgiFactory5 dxgiFactory;
	void* videoCallbackData;
	std::function<void(void*, const VideoFrame&)> videoCallback;

	// Valid after initDevice
	DxgiAdapter1 adapter;
	DxgiOutput5 output;
	D3D11Device device;
	D3D11DeviceContext context;
	MFDxgiDeviceManager deviceManager;

	// Valid after initDuplication
	DxgiOutputDuplication outputDuplication;
	int screenWidth, screenHeight;
	DXGI_FORMAT screenFormat;
	bool frameAcquired = false;

	// Valid after initEncoder
	UINT deviceManagerResetToken;
	std::unique_ptr<ColorConvD3D> colorSpaceConv;
	MFTransform encoder;
	int encoderWidth, encoderHeight;
	std::vector<DWORD> encoderInputStreamId;
	std::vector<DWORD> encoderOutputStreamId;

	// Valid while running
	std::atomic_bool flagRun = false;
	std::thread captureThread;

	void _initDevice();
	void _initDuplication();
	void _initEncoder();
	void _runCapture();
	
	void _pushEncoderTexture(const D3D11Texture2D& tex, const long long sampleDur, const long long sampleTime);
	std::vector<uint8_t> _popEncoderData();

public:
	struct EncodedFrame {
		std::vector<uint8_t> data;
	};

	StreamManager(decltype(videoCallback) callback, void* callbackData);
	void start();
	void stop();
};


#endif