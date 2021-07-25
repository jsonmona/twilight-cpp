#include "common/log.h"
#include "common/platform/windows/ComWrapper.h"

#include "server/StreamServer.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/msvc_sink.h>

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

	auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
	auto console_sink = std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>(spdlog::color_mode::automatic);

	std::vector<spdlog::sink_ptr> sinks;
	sinks.push_back(msvc_sink);
	sinks.push_back(console_sink);

	auto default_log = std::make_shared<spdlog::logger>("default", sinks.begin(), sinks.end());
	spdlog::set_default_logger(default_log);

	auto log = createNamedLogger("main");

	if (!setDpiAwareness())
		log->warn("Unable to set dpi awareness");

	hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	check_quit(FAILED(hr), log, "Failed to initialize COM");

	hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	check_quit(FAILED(hr), log, "Failed to start MediaFoundation");

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
