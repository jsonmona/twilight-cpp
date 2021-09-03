#include "CapturePipelineD3D.h"


CapturePipelineD3D::CapturePipelineD3D(DeviceManagerD3D _devs, int w, int h, ScaleType type) :
	log(createNamedLogger("CapturePipelineD3D")),
	capture(_devs), scale(ScaleD3D::createInstance(w, h, type, false)), encoder(_devs, w, h)
{
	scale->init(_devs.device, _devs.context);
}

CapturePipelineD3D::~CapturePipelineD3D() {
}

void CapturePipelineD3D::start() {
	capture.setOnNextFrame([this](DesktopFrame<D3D11Texture2D>&& cap) { captureNextFrame_(std::move(cap)); });
	encoder.setOnDataAvailable(writeOutput);

	encoder.start();
	capture.start(60);

	flagRun.store(true, std::memory_order_release);
	runThread = std::thread([this]() { run_(); });
}

void CapturePipelineD3D::stop() {
	flagRun.store(false, std::memory_order_release);
	runThread.join();

	capture.stop();
	encoder.stop();
}

void CapturePipelineD3D::run_() {
	HRESULT hr;
	hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	check_quit(FAILED(hr), log, "Failed to initialize COM");

	while (flagRun.load(std::memory_order_acquire)) {
		capture.poll();
		encoder.poll();
	}

	CoUninitialize();
}

void CapturePipelineD3D::captureNextFrame_(DesktopFrame<D3D11Texture2D>&& cap) {
	if (cap.desktop)
		scale->pushInput(*cap.desktop);

	DesktopFrame<D3D11Texture2D> data;
	data.desktop = std::make_shared<D3D11Texture2D>(scale->popOutput());
	data.cursorPos = std::move(cap.cursorPos);
	data.cursorShape = std::move(cap.cursorShape);
	encoder.pushFrame(std::move(data));
}
