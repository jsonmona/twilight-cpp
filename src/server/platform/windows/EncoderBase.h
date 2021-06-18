#ifndef SERVER_PLATFORM_WINDOWS_ENCODER_BASE_H_
#define SERVER_PLATFORM_WINDOWS_ENCODER_BASE_H_


#include "common/platform/windows/ComWrapper.h"

#include <functional>
#include <atomic>
#include <vector>

class EncoderBase {
protected:
	std::funtion<D3D11Texture2D()> _onInputNeeded;
	std::funtion<void(std::vector<uint8_t>)> _onOutput;

public:
	virtual void init(const std::funtion<D3D11Texture2D()>& onInputNeeded, const std::funtion<void(std::vector<uint8_t>)>& onOutput) {
		_onInputNeeded = onInputNeeded;
		_onOutput = onOutput;
	}

	void runEncoder(const std::atomic<bool>& flagQuit) = 0;

	virtual void deinit() {
		onInputNeeded = nullptr;
		onOutput = nullptr;
	}
};


#endif