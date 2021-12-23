#ifndef COMMON_OPENH264LOADER_WIN32_H_
#define COMMON_OPENH264LOADER_WIN32_H_

#include <atomic>

#include <common/log.h>
#include <common/platform/software/OpenH264Loader.h>

#include <common/platform/windows/winheaders.h>

class OpenH264LoaderWin32 : public OpenH264Loader {
public:
    OpenH264LoaderWin32();
    ~OpenH264LoaderWin32();

    void prepare() override;
    bool isReady() const override;

    int CreateSVCEncoder(ISVCEncoder **ppEncoder) const override;
    void DestroySVCEncoder(ISVCEncoder *pEncoder) const override;

    long CreateDecoder(ISVCDecoder **ppDecoder) const override;
    void DestroyDecoder(ISVCDecoder *pDecoder) const override;

    OpenH264Version GetCodecVersion(void) const override;
    void GetCodecVersionEx(OpenH264Version *pVersion) const override;

private:
    LoggerPtr log;

    std::atomic<bool> ready = false;

    HINSTANCE hInst = 0;

    int (*__cdecl CreateSVCEncoderProc)(ISVCEncoder **ppEncoder) = nullptr;
    void (*__cdecl DestroySVCEncoderProc)(ISVCEncoder *pEncoder) = nullptr;

    long (*__cdecl CreateDecoderProc)(ISVCDecoder **ppDecoder) = nullptr;
    void (*__cdecl DestroyDecoderProc)(ISVCDecoder *pDecoder) = nullptr;

    OpenH264Version (*__cdecl GetCodecVersionProc)(void) = nullptr;
    void (*__cdecl GetCodecVersionExProc)(OpenH264Version *pVersion) = nullptr;
};

#endif