#ifndef SERVER_PLATFORM_WINDOWS_CAPTURE_D3D_H_
#define SERVER_PLATFORM_WINDOWS_CAPTURE_D3D_H_

#include "common/log.h"
#include "common/platform/windows/ComWrapper.h"

#include "DeviceManagerD3D.h"
#include "ColorConvD3D.h"

#include <packet.pb.h>

#include <atomic>
#include <vector>
#include <thread>
#include <cstdint>
#include <mutex>
#include <functional>


struct CaptureDataD3D {
	bool desktopUpdated = false;
	// Valid until next fetch
	D3D11Texture2D desktopImage;

	bool cursorUpdated = false;
	bool cursorVisible;
	int cursorX, cursorY;

	bool cursorShapeUpdated = false;
	std::vector<uint8_t> cursorImage;
	int cursorW, cursorH;
	float hotspotX, hotspotY;
};

struct ExtraData;

class CaptureD3D {
	// Valid after constructor
	LoggerPtr log;

	std::shared_ptr<DeviceManagerD3D> devs;

	bool frameAcquired = false;
	DxgiOutputDuplication outputDuplication;

public:
	struct EncodedFrame {
		std::vector<uint8_t> data;
	};

	CaptureD3D(const decltype(devs)& _deviceManager);
	void begin();
	CaptureDataD3D fetch();
	void end();
};


#endif