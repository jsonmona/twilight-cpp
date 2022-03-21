#ifndef TWILIGHT_CLIENT_PLATFORM_SOFTWARE_DECODEROPENH264_H
#define TWILIGHT_CLIENT_PLATFORM_SOFTWARE_DECODEROPENH264_H

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "common/ByteBuffer.h"
#include "common/DesktopFrame.h"
#include "common/ffmpeg-headers.h"
#include "common/log.h"
#include "common/platform/software/OpenH264Loader.h"
#include "common/platform/software/ScaleSoftware.h"
#include "common/platform/software/TextureSoftware.h"
#include "common/util.h"

#include "client/NetworkClock.h"

class DecoderOpenH264 {
public:
    explicit DecoderOpenH264(NetworkClock& clock);
    ~DecoderOpenH264();

    void start();
    void stop();

    void setOutputResolution(int width, int height);

    void pushData(DesktopFrame<ByteBuffer>&& nextData);
    DesktopFrame<TextureSoftware> popData();

private:
    LoggerPtr log;

    NetworkClock& clock;
    std::shared_ptr<OpenH264Loader> loader;
    ScaleSoftware scale;

    bool idrPacketInQueue;
    int outputWidth, outputHeight;

    std::atomic<bool> flagRun;
    std::thread looper;

    std::mutex packetLock;
    std::condition_variable packetCV;
    std::deque<DesktopFrame<ByteBuffer>> packetQueue;

    std::mutex frameLock;
    std::condition_variable frameCV;
    std::deque<DesktopFrame<TextureSoftware>> frameQueue;
    MinMaxTrackingRingBuffer<size_t, 32> frameBufferHistory;

    void run_();
};

#endif
