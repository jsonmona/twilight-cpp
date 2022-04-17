#ifndef TWILIGHT_SERVER_AUDIOENCODER_H
#define TWILIGHT_SERVER_AUDIOENCODER_H

#include "common/ByteBuffer.h"
#include "common/ffmpeg-headers.h"
#include "common/log.h"

#include "server/platform/windows/AudioCaptureWASAPI.h"

#include <opus.h>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

class AudioEncoder {
public:
    AudioEncoder();
    ~AudioEncoder();

    template <class Fn>
    void setOnAudioData(Fn fn) {
        onAudioData = std::move(fn);
    }

    void start();
    void stop();

private:
    static NamedLogger log;

    std::atomic<bool> flagRun;

    std::unique_ptr<AudioCaptureWASAPI> cap;
    std::thread workerThread;

    std::function<void(const uint8_t*, size_t)> onAudioData;  // (data, len)

    SwrContext* swrCtx = nullptr;
    OpusEncoder* enc = nullptr;

    std::mutex bufferLock;
    std::condition_variable bufferLockCV;
    std::deque<ByteBuffer> buffer;
    int samplingRate;
    int channels;

    void runWorker_();
};

#endif
