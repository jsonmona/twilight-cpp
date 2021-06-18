#include "common/platform/windows/ComWrapper.h"
#include "StreamManager.h"

#include "common/log.h"

#include <packets.pb.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/msvc_sink.h>

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

	FILE* f = fopen("../server-stream.dump", "wb");
	check_quit(f == nullptr, log, "Failed to open file to write");

	StreamManager streamManager([&](void* _data, const VideoFrame& frame) {
		int_fast32_t len = frame.desktopImage.size();
		fwrite(&len, 4, 1, f);
		int offset = 0;
		while (offset < frame.desktopImage.size()) {
			int w = fwrite(frame.desktopImage.data() + offset, 1, frame.desktopImage.size() - offset, f);
			check_quit(w <= 0, log, "Failed to write data to file");
			offset += w;
		}
		fflush(f);
	}, nullptr);

	streamManager.start();
	Sleep(10 * 1000);
	streamManager.stop();

	MFShutdown();
}