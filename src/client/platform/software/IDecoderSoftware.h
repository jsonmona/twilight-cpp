#ifndef TWILIGHT_CLIENT_PLATFORM_SOFTWARE_IDECODERSOFTWARE_H
#define TWILIGHT_CLIENT_PLATFORM_SOFTWARE_IDECODERSOFTWARE_H

#include "common/platform/software/TextureSoftware.h"

#include "client/IDecoder.h"
#include "client/NetworkClock.h"

class IDecoderSoftware : public IDecoder {
public:
    IDecoderSoftware() = default;
    IDecoderSoftware(const IDecoderSoftware& copy) = delete;
    IDecoderSoftware(IDecoderSoftware&& move) = delete;
    virtual ~IDecoderSoftware() = default;

    virtual void init(CodecType codecType, std::shared_ptr<NetworkClock> clock) = 0;

    virtual bool readSoftware(DesktopFrame<TextureSoftware>* output) = 0;
};

#endif
