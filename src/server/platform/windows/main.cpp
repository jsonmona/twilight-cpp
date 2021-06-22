#include "common/platform/windows/ComWrapper.h"

#include "CaptureD3D.h"
#include "EncoderD3D.h"

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

	std::shared_ptr<DeviceManagerD3D> devs = std::make_shared<DeviceManagerD3D>();
	
	CaptureD3D capture(devs);
	EncoderD3D encoder(devs, 1920, 1080);

	encoder.setFrameRequestCallback([&]() -> CaptureDataD3D {
		return capture.fetch();
	});

	encoder.setDataAvailableCallback([&](EncoderDataD3D* data) {
		std::string buf;
		msg::Packet packet;
		if (data->cursorShapeUpdated) {
			auto m = packet.mutable_cursor_shape();
			m->set_image_len(data->cursorImage.size());
			m->set_width(data->cursorW);
			m->set_height(data->cursorH);
			m->set_hotspot_x(data->hotspotX);
			m->set_hotspot_y(data->hotspotY);

			packet.SerializeToString(&buf);
			int32_t packetLen = buf.size();
			writeToFile(&packetLen, 4, f);
			writeToFile(buf.data(), buf.size(), f);
			writeToFile(data->cursorImage.data(), data->cursorImage.size(), f);
		}

		auto d = packet.mutable_desktop_frame();
		d->set_image_len(data->desktopImage.size());
		d->set_cursor_visible(data->cursorVisible);
		if (data->cursorVisible) {
			d->set_cursor_x(data->cursorX);
			d->set_cursor_y(data->cursorY);
		}

		packet.SerializeToString(&buf);
		int32_t packetLen = buf.size();
		writeToFile(&packetLen, 4, f);
		writeToFile(buf.data(), buf.size(), f);
		writeToFile(data->desktopImage.data(), data->desktopImage.size(), f);

		fflush(f);
		static int cnt = 0;
		log->debug("Written frame {}", cnt++);
	});

	capture.begin();
	encoder.start();
	Sleep(1200 * 1000);
	encoder.stop();
	capture.end();

	MFShutdown();
}