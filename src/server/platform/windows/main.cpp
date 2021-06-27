#include "common/log.h"
#include "common/platform/windows/ComWrapper.h"

#include "server/StreamServer.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/msvc_sink.h>

#include <string>
#include <memory>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <chrono>


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	HRESULT hr;

	auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
	auto default_log = std::make_shared<spdlog::logger>("default", msvc_sink);
	spdlog::set_default_logger(default_log);

	auto log = createNamedLogger("main");

	//TODO: Use manifest
	if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
		log->warn("Unable to set dpi awareness");

	hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	check_quit(FAILED(hr), log, "Failed to initialize COM");

	hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	check_quit(FAILED(hr), log, "Failed to start MediaFoundation");

	StreamServer stream;

	stream.start();
	Sleep(10 * 1000);
	stream.stop();

	MFShutdown();
}