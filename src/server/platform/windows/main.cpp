#include "common/log.h"
#include "common/platform/windows/ComWrapper.h"

#include "server/StreamServer.h"

#include "server/platform/windows/CaptureD3D.h"

#include <string>
#include <memory>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <chrono>

typedef BOOL (WINAPI *FnSetProcessDpiAwarenessContext)(HANDLE);
static const HANDLE dpiAwarenessContextPerMonitorAwareV2 = reinterpret_cast<HANDLE>(0xfffffffffffffffc);


static bool setDpiAwareness() {
	bool success = false;

	HMODULE user32 = LoadLibrary(L"user32.dll");
	if (user32 != nullptr) {
		auto fn = (FnSetProcessDpiAwarenessContext)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
		if (fn != nullptr)
			success = fn(dpiAwarenessContextPerMonitorAwareV2);
		FreeLibrary(user32);
		user32 = nullptr;
	}

	if (!success)
		success = SetProcessDPIAware();

	return success;
}


int main() {
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	HRESULT hr;

	auto log = createNamedLogger("main");

	if (!setDpiAwareness())
		log->warn("Unable to set dpi awareness");

	hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	check_quit(FAILED(hr), log, "Failed to initialize COM");

	hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	check_quit(FAILED(hr), log, "Failed to start MediaFoundation");

	setupFFmpegLogs();

	StreamServer stream;

	log->info("Starting daylight streaming server...");

	stream.start();
	Sleep(3600 * 1000);

	log->info("Stopping daylight streaming server...");
	stream.stop();

	MFShutdown();
	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	return main();
}
