#ifndef TWILIGHT_CLIENT_PLATFORM_SOFTWARE_DECODERFFMPEG_H
#define TWILIGHT_CLIENT_PLATFORM_SOFTWARE_DECODERFFMPEG_H

#include "common/DesktopFrame.h"
#include "common/ffmpeg-headers.h"
#include "common/log.h"
#include "common/CircularDeque.h"

#include "common/platform/software/ScaleSoftware.h"
#include "common/platform/software/TextureSoftware.h"

#include "client/NetworkClock.h"

#include "client/platform/software/IDecoderSoftware.h"

#include <deque>
#include <mutex>

class DecoderFFmpeg : public IDecoderSoftware {
public:
    DecoderFFmpeg();
    ~DecoderFFmpeg();

    std::vector<CodecType> enumSupportedCodecs() override;

    void setVideoResolution(int width, int height) override;

    void init(CodecType codecType_, std::shared_ptr<NetworkClock> clock_) override;

    void start() override;
    void stop() override;

    void pushData(DesktopFrame<ByteBuffer>&& frame) override;
    bool readSoftware(DesktopFrame<TextureSoftware>* output) override;

private:
    void run_();

    static NamedLogger log;

    std::shared_ptr<NetworkClock> clock;

    std::atomic<bool> flagRun;
    bool flagKeyInPacket;
    bool flagNextFrameAvailable;

    CodecType codecType;

    const AVCodec* codec;
    AVCodecContext* avctx;

    std::thread runThread;

    std::mutex packetLock;
    std::condition_variable packetCV;
    std::deque<DesktopFrame<ByteBuffer>> packetQueue;

    std::mutex frameLock;
    std::condition_variable frameCV;
    CircularDeque<DesktopFrame<AVFramePtr>, 4> frameQueue;
    AVFramePtr prevReadFrame;
};

#endif
