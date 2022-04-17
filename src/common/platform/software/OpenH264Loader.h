#ifndef TWILIGHT_COMMON_PLATFORM_SOFTWARE_OPENH264LOADER_H
#define TWILIGHT_COMMON_PLATFORM_SOFTWARE_OPENH264LOADER_H

#include "common/log.h"

#include <openh264/codec_api.h>

#include <memory>
#include <mutex>

class OpenH264Loader {
public:
    static std::shared_ptr<OpenH264Loader> getInstance();

    OpenH264Loader();
    virtual ~OpenH264Loader();

    virtual void prepare() = 0;
    virtual bool isReady() const = 0;

    virtual int CreateSVCEncoder(ISVCEncoder **ppEncoder) const = 0;
    virtual void DestroySVCEncoder(ISVCEncoder *pEncoder) const = 0;

    virtual long CreateDecoder(ISVCDecoder **ppDecoder) const = 0;
    virtual void DestroyDecoder(ISVCDecoder *pDecoder) const = 0;

    virtual OpenH264Version GetCodecVersion(void) const = 0;
    virtual void GetCodecVersionEx(OpenH264Version *pVersion) const = 0;

protected:
    bool checkVersion() const;

private:
    static NamedLogger log;
    static std::weak_ptr<OpenH264Loader> instance;
    static std::mutex instanceLock;
};

#endif
