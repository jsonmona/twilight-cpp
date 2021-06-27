#ifndef SERVER_PLATFORM_WINDOWS_CAPTURE_D3D_H_
#define SERVER_PLATFORM_WINDOWS_CAPTURE_D3D_H_

#include "common/log.h"
#include "server/CaptureData.h"
#include "common/platform/windows/ComWrapper.h"

#include "DeviceManagerD3D.h"

#include <atomic>
#include <vector>
#include <thread>
#include <cstdint>
#include <mutex>
#include <functional>


class CaptureD3D {
	// Valid after constructor
	LoggerPtr log;

	bool initialized = false;
	DeviceManagerD3D devs;

	bool frameAcquired = false;
	DxgiOutputDuplication outputDuplication;

public:
	struct EncodedFrame {
		std::vector<uint8_t> data;
	};

	CaptureD3D(DeviceManagerD3D _devs);
	void begin();
	CaptureData<D3D11Texture2D> fetch();
	void end();
};


#endif