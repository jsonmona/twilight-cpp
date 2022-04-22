#ifndef TWILIGHT_CLIENT_PLATFORM_SOFTWARE_DECODEROPENH264_H
#define TWILIGHT_CLIENT_PLATFORM_SOFTWARE_DECODEROPENH264_H

#include "common/ByteBuffer.h"
#include "common/DesktopFrame.h"
#include "common/ffmpeg-headers.h"
#include "common/log.h"
#include "common/util.h"
#include "common/CircularDeque.h"

#include "common/platform/software/OpenH264Loader.h"
#include "common/platform/software/ScaleSoftware.h"
#include "common/platform/software/TextureSoftware.h"

#include "client/NetworkClock.h"

#include "client/platform/software/IDecoderSoftware.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

class DecoderOpenH264 : public IDecoderSoftware {
public:
    DecoderOpenH264();
    ~DecoderOpenH264();

    std::vector<CodecType> enumSupportedCodecs() override;

    void setVideoResolution(int width, int height) override;

    void init(CodecType codecType, std::shared_ptr<NetworkClock> clock) override;

    void start() override;
    void stop() override;

    void pushData(DesktopFrame<ByteBuffer>&& frame) override;
    bool readSoftware(DesktopFrame<TextureSoftware>* output) override;

private:
    void run_();

    static NamedLogger log;

    std::shared_ptr<NetworkClock> clock;
    std::shared_ptr<OpenH264Loader> loader;

    std::thread runThread;

    std::atomic<bool> flagRun;
    int width, height;

    std::mutex packetLock;
    std::condition_variable packetCV;
    std::deque<DesktopFrame<ByteBuffer>> packetQueue;

    std::mutex frameLock;
    std::condition_variable frameCV;
    CircularDeque<DesktopFrame<TextureSoftware>, 4> frameQueue;
};

#endif
