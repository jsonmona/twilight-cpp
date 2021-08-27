#ifndef SERVER_PLATFORM_WINDOWS_ENCODER_D3D_H_
#define SERVER_PLATFORM_WINDOWS_ENCODER_D3D_H_


#include "common/log.h"
#include "common/ByteBuffer.h"
#include "common/DesktopFrame.h"
#include "common/StatisticMixer.h"

#include "common/platform/windows/DeviceManagerD3D.h"

#include <atomic>
#include <memory>
#include <vector>
#include <deque>
#include <functional>


class EncoderD3D {
	struct SideData {
		long long pts;
		std::chrono::steady_clock::time_point inputTime;
	};

	LoggerPtr log;

	std::atomic<bool> flagWaitingInput;
	int width, height;
	long long frameCnt;

	std::function<void(DesktopFrame<ByteBuffer>&&)> onDataAvailable;

	std::deque<DesktopFrame<SideData>> extraData;

	MFDxgiDeviceManager mfDeviceManager;
	MFTransform encoder;
	DWORD inputStreamId, outputStreamId;
	StatisticMixer statMixer;

	std::thread workerThread;
	std::mutex dataPushLock;
	std::condition_variable dataPushCV;

	void init_();
	void run_();

	void pushEncoderTexture_(const D3D11Texture2D& tex, long long sampleDur, long long sampleTime);
	ByteBuffer popEncoderData_(long long* sampleTime);

public:
	EncoderD3D(DeviceManagerD3D _devs, int _width, int _height);
	~EncoderD3D();

	template<typename Fn>
	void setOnDataAvailable(Fn fn) { onDataAvailable = std::move(fn); }

	void start();
	void stop();

	StatisticMixer::Stat calcEncoderStat() { return statMixer.calcStat(); }

	void pushData(DesktopFrame<D3D11Texture2D>&& cap);
};


#endif