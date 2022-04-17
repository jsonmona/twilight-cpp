#include "OpenH264LoaderWin32.h"

TWILIGHT_DEFINE_LOGGER(OpenH264LoaderWin32);

OpenH264LoaderWin32::OpenH264LoaderWin32() {}

OpenH264LoaderWin32::~OpenH264LoaderWin32() {
    if (hInst != 0)
        FreeLibrary(hInst);
}

void OpenH264LoaderWin32::prepare() {
    hInst = LoadLibrary(TEXT("openh264-2.1.1-win64.dll"));
    log.assert_quit(hInst != 0, "Failed to load openh264-2.1.1-win64.dll library!");
    log.critical("LICENSE NOTICE: OpenH264 Video Codec provided by Cisco Systems, Inc.");

    CreateSVCEncoderProc = (decltype(CreateSVCEncoderProc))GetProcAddress(hInst, "WelsCreateSVCEncoder");
    log.assert_quit(CreateSVCEncoderProc != nullptr, "Failed to load WelsCreateSVCEncoder!");

    DestroySVCEncoderProc = (decltype(DestroySVCEncoderProc))GetProcAddress(hInst, "WelsDestroySVCEncoder");
    log.assert_quit(DestroySVCEncoderProc != nullptr, "Failed to load WelsDestroySVCEncoder!");

    CreateDecoderProc = (decltype(CreateDecoderProc))GetProcAddress(hInst, "WelsCreateDecoder");
    log.assert_quit(CreateDecoderProc != nullptr, "Failed to load WelsCreateDecoder!");

    DestroyDecoderProc = (decltype(DestroyDecoderProc))GetProcAddress(hInst, "WelsDestroyDecoder");
    log.assert_quit(DestroyDecoderProc != nullptr, "Failed to load WelsDestroyDecoder!");

    GetCodecVersionProc = (decltype(GetCodecVersionProc))GetProcAddress(hInst, "WelsGetCodecVersion");
    log.assert_quit(GetCodecVersionProc != nullptr, "Failed to load WelsGetCodecVersion!");

    GetCodecVersionExProc = (decltype(GetCodecVersionExProc))GetProcAddress(hInst, "WelsGetCodecVersionEx");
    log.assert_quit(GetCodecVersionExProc != nullptr, "Failed to load WelsGetCodecVersionEx!");

    checkVersion();

    ready.store(true, std::memory_order_release);
}

bool OpenH264LoaderWin32::isReady() const {
    return ready.load(std::memory_order_acquire);
}

int OpenH264LoaderWin32::CreateSVCEncoder(ISVCEncoder **ppEncoder) const {
    return CreateSVCEncoderProc(ppEncoder);
}

void OpenH264LoaderWin32::DestroySVCEncoder(ISVCEncoder *pEncoder) const {
    return DestroySVCEncoderProc(pEncoder);
}

long OpenH264LoaderWin32::CreateDecoder(ISVCDecoder **ppDecoder) const {
    return CreateDecoderProc(ppDecoder);
}

void OpenH264LoaderWin32::DestroyDecoder(ISVCDecoder *pDecoder) const {
    return DestroyDecoderProc(pDecoder);
}

OpenH264Version OpenH264LoaderWin32::GetCodecVersion(void) const {
    return GetCodecVersionProc();
}

void OpenH264LoaderWin32::GetCodecVersionEx(OpenH264Version *pVersion) const {
    return GetCodecVersionExProc(pVersion);
}
