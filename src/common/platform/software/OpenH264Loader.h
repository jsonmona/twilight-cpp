#ifndef COMMON_OPENH264LOADER_H_
#define COMMON_OPENH264LOADER_H_

#include <memory>
#include <mutex>

#include <openh264/codec_api.h>

#include <common/log.h>

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
    bool checkVersion(const LoggerPtr &log) const;

private:
    static std::weak_ptr<OpenH264Loader> instance;
    static std::mutex instanceLock;
};

#endif