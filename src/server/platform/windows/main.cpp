#include "common/platform/windows/ComWrapper.h"
#include "CaptureManagerD3D.h"

#include "common/log.h"

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

	FILE* f = fopen("../server-stream.dump", "wb");
	check_quit(f == nullptr, log, "Failed to open file to write");

	auto writeToFile = [](const void* data, int length, FILE *f) {
		const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
		int pos = 0;
		while (pos < length) {
			int ret = fwrite(ptr + pos, 1, length - pos, f);
			if(ret <= 0)
				abort();
			pos += ret;
		}
	};

	CaptureManagerD3D streamManager([&](void* _data, VideoInfo* video, CursorInfo* cursor) {
		std::string buf;
		packets::Packet packet;
		if (cursor) {
			auto m = packet.mutable_cursor_shape();
			m->set_cursor_image_len(cursor->cursorImage.size());
			m->set_width(cursor->width);
			m->set_height(cursor->height);
			m->set_cursor_hotspot_x(cursor->hotspotX);
			m->set_cursor_hotspot_y(cursor->hotspotY);

			packet.SerializeToString(&buf);
			int32_t packetLen = buf.size();
			writeToFile(&packetLen, 4, f);
			writeToFile(buf.data(), buf.size(), f);
			writeToFile(cursor->cursorImage.data(), cursor->cursorImage.size(), f);
		}
		if (video) {
			auto m = packet.mutable_video_frame();
			m->set_desktop_image_len(video->desktopImage.size());
			m->set_cursor_visible(video->cursorVisible);
			if (video->cursorVisible) {
				m->set_cursor_x(video->cursorPosX);
				m->set_cursor_y(video->cursorPosY);
			}

			packet.SerializeToString(&buf);
			int32_t packetLen = buf.size();
			writeToFile(&packetLen, 4, f);
			writeToFile(buf.data(), buf.size(), f);
			writeToFile(video->desktopImage.data(), video->desktopImage.size(), f);
		}
		fflush(f);
	}, nullptr);

	streamManager.start();
	Sleep(10 * 1000);
	streamManager.stop();

	MFShutdown();
}