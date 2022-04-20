#ifndef TWILIGHT_SERVER_PLATFORM_SOFTWARE_ENCODERFFMPEG_H
#define TWILIGHT_SERVER_PLATFORM_SOFTWARE_ENCODERFFMPEG_H

#include "common/DesktopFrame.h"
#include "common/Rational.h"
#include "common/ffmpeg-headers.h"
#include "common/log.h"

#include "common/platform/software/TextureSoftware.h"

#include "server/LocalClock.h"

#include <queue>

class EncoderFFmpeg {
public:
    explicit EncoderFFmpeg(LocalClock& clock);
    ~EncoderFFmpeg();

    void setMode(int width, int height, Rational framerate);

    void start();
    void stop();

    void pushFrame(DesktopFrame<TextureSoftware>&& frame);
    bool readData(DesktopFrame<ByteBuffer>* output);

private:
    void run_();

    static NamedLogger log;

    LocalClock& clock;

    std::atomic<bool> flagRun;

    CodecType codecType;
    int width, height;
    Rational framerate;

    const AVCodec* codec;
    AVCodecContext* avctx;

    std::thread runThread;

    std::mutex frameLock;
    std::condition_variable frameCV;
    DesktopFrame<TextureSoftware> nextFrame;

    std::mutex packetLock;
    std::condition_variable packetCV;
    std::deque<DesktopFrame<ByteBuffer>> packetQueue;

    bool flagNextFrameAvailable;
    bool flagNextPacketAvailable;
};

#endif
