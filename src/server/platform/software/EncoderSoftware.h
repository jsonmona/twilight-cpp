#ifndef SERVER_PLATFORM_SOFTWARE_ENCODER_SOFTWARE_H_
#define SERVER_PLATFORM_SOFTWARE_ENCODER_SOFTWARE_H_


#include "common/log.h"
#include "common/ByteBuffer.h"
#include "common/ffmpeg-headers.h"
#include "common/DesktopFrame.h"
#include "common/StatisticMixer.h"

#include "common/platform/software/OpenH264Loader.h"
#include "common/platform/software/TextureSoftware.h"

#include <deque>


class EncoderSoftware {
public:
	EncoderSoftware(int _width, int _height);
	~EncoderSoftware();

	template<typename Fn>
	void setDataAvailableCallback(Fn fn) { onDataAvailable = std::move(fn); }

	void start();
	void stop();

	StatisticMixer::Stat calcEncoderStat() { return statMixer.calcStat(); }

	void pushData(DesktopFrame<TextureSoftware>&& newData);

private:
	LoggerPtr log;

	std::function<void(DesktopFrame<ByteBuffer>&&)> onDataAvailable;

	std::shared_ptr<OpenH264Loader> loader;
	int width, height;

	std::atomic<bool> flagRun;

	std::thread runThread;
	std::mutex dataLock;
	std::condition_variable dataCV;
	std::deque<DesktopFrame<TextureSoftware>> dataQueue;

	StatisticMixer statMixer;

	void run_();
};


#endif