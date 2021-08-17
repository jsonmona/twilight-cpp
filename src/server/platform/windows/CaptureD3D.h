#ifndef SERVER_PLATFORM_WINDOWS_CAPTURE_D3D_H_
#define SERVER_PLATFORM_WINDOWS_CAPTURE_D3D_H_

#include "common/log.h"

#include "common/platform/windows/ComWrapper.h"
#include "common/platform/windows/DeviceManagerD3D.h"

#include "server/CaptureData.h"

#include <atomic>
#include <vector>
#include <thread>
#include <cstdint>
#include <functional>
#include <chrono>


class CaptureD3D {
	LoggerPtr log;

	int fps;
	bool frameAcquired;
	bool firstFrameSent;
	std::atomic<bool> flagRun;

	DxgiOutput5 output;
	D3D11Device device;
	DxgiOutputDuplication outputDuplication;

	std::thread runThread;
	long long perfCounterFreq;
	long long frameInterval;

	std::function<void(CaptureData<D3D11Texture2D>&&)> onNextFrame;

	bool tryReleaseFrame_();
	bool openDuplication_();
	void run_();
	CaptureData<D3D11Texture2D> captureFrame_();
	void parseCursor_(CursorShapeData* cursorShape, const DXGI_OUTDUPL_POINTER_SHAPE_INFO& cursorInfo, const std::vector<uint8_t>& buffer);

public:
	CaptureD3D(DeviceManagerD3D _devs);
	~CaptureD3D();

	void start(int fps);
	void stop();

	template<typename Fn>
	void setOnNextFrame(Fn fn) { onNextFrame = std::move(fn); }
};


#endif