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


class CaptureD3D {
	LoggerPtr log;

	bool firstFrameSent;
	bool frameAcquired;

	DxgiOutput5 output;
	D3D11Device device;
	DxgiOutputDuplication outputDuplication;

	void openDuplication_();
	void parseCursor_(CursorShapeData* cursorShape, const DXGI_OUTDUPL_POINTER_SHAPE_INFO& cursorInfo, const std::vector<uint8_t>& buffer);

public:
	CaptureD3D(DeviceManagerD3D _devs);
	~CaptureD3D();

	void start();
	void stop();

	CaptureData<D3D11Texture2D> poll();
};


#endif