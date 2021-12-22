#include "OpenH264LoaderWin32.h"


OpenH264LoaderWin32::OpenH264LoaderWin32() :
	log(createNamedLogger("OpenH264LoaderWin32"))
{
}

OpenH264LoaderWin32::~OpenH264LoaderWin32() {
	if (hInst != 0)
		FreeLibrary(hInst);
}

void OpenH264LoaderWin32::prepare() {
	hInst = LoadLibrary(TEXT("openh264-2.1.1-win64.dll"));
	check_quit(hInst == 0, log, "Failed to load openh264-2.1.1-win64.dll library!");
	log->critical("LICENSE NOTICE: OpenH264 Video Codec provided by Cisco Systems, Inc.");

	CreateSVCEncoderProc = (decltype(CreateSVCEncoderProc)) GetProcAddress(hInst, "WelsCreateSVCEncoder");
	check_quit(CreateSVCEncoderProc == nullptr, log, "Failed to load CreateSVCEncoder!");

	DestroySVCEncoderProc = (decltype(DestroySVCEncoderProc)) GetProcAddress(hInst, "WelsDestroySVCEncoder");
	check_quit(DestroySVCEncoderProc == nullptr, log, "Failed to load DestroySVCEncoder!");

	CreateDecoderProc = (decltype(CreateDecoderProc)) GetProcAddress(hInst, "WelsCreateDecoder");
	check_quit(CreateDecoderProc == nullptr, log, "Failed to load CreateDecoder!");

	DestroyDecoderProc = (decltype(DestroyDecoderProc)) GetProcAddress(hInst, "WelsDestroyDecoder");
	check_quit(DestroyDecoderProc == nullptr, log, "Failed to load DestroyDecoder!");

	GetCodecVersionProc = (decltype(GetCodecVersionProc)) GetProcAddress(hInst, "WelsGetCodecVersion");
	check_quit(GetCodecVersionProc == nullptr, log, "Failed to load GetCodecVersion!");

	GetCodecVersionExProc = (decltype(GetCodecVersionExProc)) GetProcAddress(hInst, "WelsGetCodecVersionEx");
	check_quit(GetCodecVersionExProc == nullptr, log, "Failed to load GetCodecVersionEx!");

	checkVersion(log);

	ready.store(true, std::memory_order_release);
}

bool OpenH264LoaderWin32::isReady() const {
	return ready.load(std::memory_order_acquire);
}

int OpenH264LoaderWin32::CreateSVCEncoder(ISVCEncoder** ppEncoder) const {
	return CreateSVCEncoderProc(ppEncoder);
}

void OpenH264LoaderWin32::DestroySVCEncoder(ISVCEncoder* pEncoder) const {
	return DestroySVCEncoderProc(pEncoder);
}

long OpenH264LoaderWin32::CreateDecoder(ISVCDecoder** ppDecoder) const {
	return CreateDecoderProc(ppDecoder);
}

void OpenH264LoaderWin32::DestroyDecoder(ISVCDecoder* pDecoder) const {
	return DestroyDecoderProc(pDecoder);
}

OpenH264Version OpenH264LoaderWin32::GetCodecVersion(void) const {
	return GetCodecVersionProc();
}

void OpenH264LoaderWin32::GetCodecVersionEx(OpenH264Version* pVersion) const {
	return GetCodecVersionExProc(pVersion);
}
