#ifndef SERVER_PLATFORM_WINDOWS_AUDIO_CAPTURE_WASAPI
#define SERVER_PLATFORM_WINDOWS_AUDIO_CAPTURE_WASAPI


#include "common/log.h"
#include "common/ByteBuffer.h"
#include "common/ffmpeg-headers.h"

#include "common/platform/windows/ComWrapper.h"

#include <thread>
#include <atomic>


class AudioCaptureWASAPI {
public:
	AudioCaptureWASAPI();
	~AudioCaptureWASAPI();

	void start();
	void stop();

	template<typename Fn>
	void setOnConfigured(Fn fn) { onConfigured = std::move(fn); }

	template<typename Fn>
	void setOnAudioData(Fn fn) { onAudioData = std::move(fn); }

private:
	LoggerPtr log;

	std::function<void(AVSampleFormat, int, int)> onConfigured;  // (format, samplingRate, channels)
	std::function<void(const uint8_t*, size_t)> onAudioData; // (data, len_bytes)
	std::thread recordThread;
	std::thread playbackThread;

	std::atomic<bool> flagRun;

	void runRecord_();
	void runPlayback_();
};


#endif