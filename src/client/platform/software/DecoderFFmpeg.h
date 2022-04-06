#ifndef TWILIGHT_CLIENT_PLATFORM_SOFTWARE_DECODERFFMPEG_H
#define TWILIGHT_CLIENT_PLATFORM_SOFTWARE_DECODERFFMPEG_H

#include <queue>
#include <thread>

#include "common/DesktopFrame.h"
#include "common/ffmpeg-headers.h"
#include "common/log.h"

#include "common/platform/software/ScaleSoftware.h"
#include "common/platform/software/TextureSoftware.h"

#include "client/NetworkClock.h"

class DecoderFFmpeg {
public:
    explicit DecoderFFmpeg(NetworkClock& clock);
    ~DecoderFFmpeg();

    void setOutputResolution(int width, int height);

    void start();
    void stop();

    void pushData(DesktopFrame<ByteBuffer>&& frame);
    bool readFrame(DesktopFrame<TextureSoftware>* output);

private:
    void run_();

    LoggerPtr log;
    NetworkClock& clock;

    std::atomic<bool> flagRun;

    CodecType codecType;
    int width, height;

    const AVCodec* codec;
    AVCodecContext* avctx;
    ScaleSoftware scale;

    std::thread runThread;

    std::mutex packetLock;
    std::condition_variable packetCV;
    std::deque<DesktopFrame<ByteBuffer>> packetQueue;

    std::mutex frameLock;
    std::condition_variable frameCV;
    DesktopFrame<TextureSoftware> nextFrame;

    bool flagKeyInPacket;
    bool flagNextFrameAvailable;
};

#endif