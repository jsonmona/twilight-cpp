#ifndef TWILIGHT_COMMON_PLATFORM_WINDOWS_OPENH264LOADERWIN32_H
#define TWILIGHT_COMMON_PLATFORM_WINDOWS_OPENH264LOADERWIN32_H

#ifdef __GNUC__
#define TWILIGHT_CDECL __attribute__((__cdecl__))
#else
#define TWILIGHT_CDECL __cdecl
#endif

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

    int(TWILIGHT_CDECL *CreateSVCEncoderProc)(ISVCEncoder **ppEncoder) = nullptr;
    void(TWILIGHT_CDECL *DestroySVCEncoderProc)(ISVCEncoder *pEncoder) = nullptr;

    long(TWILIGHT_CDECL *CreateDecoderProc)(ISVCDecoder **ppDecoder) = nullptr;
    void(TWILIGHT_CDECL *DestroyDecoderProc)(ISVCDecoder *pDecoder) = nullptr;

    OpenH264Version(TWILIGHT_CDECL *GetCodecVersionProc)(void) = nullptr;
    void(TWILIGHT_CDECL *GetCodecVersionExProc)(OpenH264Version *pVersion) = nullptr;
};

#endif