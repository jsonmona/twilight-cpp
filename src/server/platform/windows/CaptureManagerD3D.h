#ifndef STREAM_MANAGER_H_
#define STREAM_MANAGER_H_

#include "common/log.h"
#include "common/platform/windows/ComWrapper.h"
#include "ColorConvD3D.h"

#include <packets.pb.h>

#include <atomic>
#include <vector>
#include <thread>
#include <cstdint>
#include <mutex>
#include <mutex>
#include <functional>


struct VideoInfo {
	std::vector<uint8_t> desktopImage;
	bool cursorVisible;
	int cursorPosX, cursorPosY;
};

struct CursorInfo {
	std::vector<uint8_t> cursorImage;
	int width, height;
	float hotspotX, hotspotY;
};

struct ExtraData;

//TODO: Use subclass to support win8.1 (dxgifactory1)
class CaptureManagerD3D {
	// Valid after constructor
	LoggerPtr log;
	DxgiFactory5 dxgiFactory;
	int width, height;
	void* videoCallbackData;
	std::function<void(void*, VideoInfo*, CursorInfo*)> videoCallback;

	// Valid after initDevice
	DxgiAdapter1 adapter;
	DxgiOutput5 output;
	D3D11Device device;
	D3D11DeviceContext context;
	MFDxgiDeviceManager deviceManager;
	UINT deviceManagerResetToken;

	// Valid after initEncoder
	std::unique_ptr<ColorConvD3D> colorSpaceConv;
	MFTransform encoder;
	DWORD inputStreamId;
	DWORD outputStreamId;

	// Valid after initDuplication
	DxgiOutputDuplication outputDuplication;
	bool frameAcquired;

	// Valid while running
	std::atomic_bool flagRun = false;
	std::thread captureThread;

	void _initDevice();
	void _initEncoder();
	void _initDuplication();
	void _runCapture();

	bool _fetchTexture(ExtraData* now, const ExtraData* prev, bool forcePush);
	
	void _pushEncoderTexture(const D3D11Texture2D& tex, const long long sampleDur, const long long sampleTime);
	std::vector<uint8_t> _popEncoderData(long long* sampleTime);

public:
	struct EncodedFrame {
		std::vector<uint8_t> data;
	};

	CaptureManagerD3D(decltype(videoCallback) callback, void* callbackData);
	void start();
	void stop();
};


#endif