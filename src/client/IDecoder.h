#ifndef TWILIGHT_CLIENT_IDECODER_H
#define TWILIGHT_CLIENT_IDECODER_H

#include "common/DesktopFrame.h"

#include <future>
#include <vector>

class IDecoder {
public:
    IDecoder() = default;
    IDecoder(const IDecoder& copy) = delete;
    IDecoder(IDecoder&& move) = delete;
    virtual ~IDecoder() = default;

    virtual std::vector<CodecType> enumSupportedCodecs() = 0;

    virtual void setVideoResolution(int width, int height) = 0;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void pushData(DesktopFrame<ByteBuffer>&& frame) = 0;
};

#endif
