#include "common/platform/windows/ComWrapper.h"

#include "server/CaptureData.h"

#include "CaptureD3D.h"
#include "ScaleD3D.h"
#include "EncoderD3D.h"

#include "server/platform/software/ScaleSoftware.h"
#include "server/platform/software/EncoderSoftware.h"

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
	//ScaleSoftware scale = ScaleSoftware(1920, 1080, ScaleType::NV12);
	//EncoderSoftware encoder = EncoderSoftware(1920, 1080);
	std::unique_ptr<ScaleD3D> scale = ScaleD3D::createInstance(ScaleType::NV12, 1920, 1080);
	EncoderD3D encoder = EncoderD3D(devs, 1920, 1080);

	std::shared_ptr<CursorData> prevCursor;

	/*encoder.setFrameRequestCallback([&]() -> CaptureData<TextureSoftware> {
		CaptureData<D3D11Texture2D> cap = capture.fetch();
		if (cap.desktop) {
			D3D11_TEXTURE2D_DESC desc = {}, orgDesc;
			(*cap.desktop)->GetDesc(&orgDesc);

			desc.Format = orgDesc.Format;
			desc.Width = orgDesc.Width;
			desc.Height = orgDesc.Height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			
			D3D11Texture2D stageTex;
			devs->device->CreateTexture2D(&desc, nullptr, stageTex.data());
			devs->context->CopyResource(stageTex.ptr(), cap.desktop->ptr());

			if (orgDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
				abort();

			TextureSoftware softTex(desc.Width, desc.Height, AV_PIX_FMT_BGRA);

			D3D11_MAPPED_SUBRESOURCE mapInfo;
			devs->context->Map(stageTex.ptr(), 0, D3D11_MAP_READ, 0, &mapInfo);

			const uint8_t* basePtr = reinterpret_cast<const uint8_t*>(mapInfo.pData);
			for (int i = 0; i < desc.Height; i++)
				memcpy(&softTex.data[0][i * softTex.linesize[0]], &basePtr[i * mapInfo.RowPitch], desc.Width * 4);

			devs->context->Unmap(stageTex.ptr(), 0);

			scale.pushInput(std::move(softTex));
		}

		CaptureData<TextureSoftware> soft;
		soft.desktop = std::make_shared<TextureSoftware>(scale.popOutput());
		soft.cursor = std::move(cap.cursor);
		soft.cursorShape = std::move(cap.cursorShape);
		return soft;
	});*/

	encoder.setFrameRequestCallback([&]() -> CaptureData<D3D11Texture2D> {
		CaptureData<D3D11Texture2D> cap = capture.fetch();
		if (cap.desktop)
			scale->pushInput(*cap.desktop);
		cap.desktop = std::make_shared<D3D11Texture2D>(scale->popOutput());
		return cap;
	});

	encoder.setDataAvailableCallback([&](CaptureData<std::vector<uint8_t>>&& data) {
		std::string buf;
		msg::Packet packet;
		if (data.cursorShape) {
			auto m = packet.mutable_cursor_shape();
			m->set_image_len(data.cursorShape->image.size());
			m->set_width(data.cursorShape->width);
			m->set_height(data.cursorShape->height);
			m->set_hotspot_x(data.cursorShape->hotspotX);
			m->set_hotspot_y(data.cursorShape->hotspotY);

			packet.SerializeToString(&buf);
			int32_t packetLen = buf.size();
			writeToFile(&packetLen, 4, f);
			writeToFile(buf.data(), buf.size(), f);
			writeToFile(data.cursorShape->image.data(), data.cursorShape->image.size(), f);
		}

		auto d = packet.mutable_desktop_frame();
		d->set_image_len(data.desktop->size());
		if (data.cursor)
			prevCursor = std::move(data.cursor);

		d->set_cursor_visible(prevCursor->visible);
		if (prevCursor->visible) {
			d->set_cursor_x(prevCursor->posX);
			d->set_cursor_y(prevCursor->posY);
		}

		packet.SerializeToString(&buf);
		int32_t packetLen = buf.size();
		writeToFile(&packetLen, 4, f);
		writeToFile(buf.data(), buf.size(), f);
		writeToFile(data.desktop->data(), data.desktop->size(), f);

		fflush(f);
		static int cnt = 0;
		log->debug("Written frame {}", cnt++);
	});

	scale->init(devs->device, devs->context);

	capture.begin();
	encoder.start();
	Sleep(10 * 1000);
	encoder.stop();
	capture.end();

	MFShutdown();
}