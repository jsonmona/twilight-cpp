#include "CapturePipelineD3D.h"


CapturePipelineD3D::CapturePipelineD3D(DeviceManagerD3D _devs, int w, int h, ScaleType type) :
	log(createNamedLogger("CapturePipelineD3D")),
	capture(_devs), scale(ScaleD3D::createInstance(w, h, type)), encoder(_devs, w, h)
{
	scale->init(_devs.device, _devs.context);
	encoder.setOnFrameRequest([this]() -> CaptureData<D3D11Texture2D> { return _fetchTexture(); });
}

CapturePipelineD3D::~CapturePipelineD3D() {
}

void CapturePipelineD3D::start() {
	encoder.setOnDataAvailable(writeOutput);

	capture.start();
	encoder.start();

	flagRun.store(true, std::memory_order_release);
	runThread = std::thread([this]() { run_(); });
}

void CapturePipelineD3D::stop() {
	flagRun.store(false, std::memory_order_release);
	runThread.join();

	encoder.stop();
	capture.stop();
}

CaptureData<D3D11Texture2D> CapturePipelineD3D::_fetchTexture() {
	CaptureData<D3D11Texture2D> ret;

	ret.desktop = std::make_shared<D3D11Texture2D>(scale->popOutput());
	ret.cursor = std::move(lastCursor);
	ret.cursorShape = std::move(lastCursorShape);

	return ret;
}

void CapturePipelineD3D::run_() {
	HRESULT hr;
	hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	check_quit(hr != S_OK, log, "Failed to initialize COM");

	const double maxDriftRatio = 0.0001;
	const int maxPrevTicks = 60;
	const std::chrono::nanoseconds encodeTicks = std::chrono::nanoseconds(1'000'000'000 / 60);

	auto oldTime = std::chrono::steady_clock::now();

	while (flagRun.load(std::memory_order_acquire)) {
		bool firstLoop = true;
		auto nowTime = std::chrono::steady_clock::now();
		while(firstLoop || nowTime - oldTime < encodeTicks) {
			firstLoop = false;
			auto awaitTime = encodeTicks - (nowTime - oldTime);
			auto awaitTimeMillis = std::chrono::duration_cast<std::chrono::milliseconds>(awaitTime) - std::chrono::milliseconds(1);
			auto now = capture.poll(awaitTimeMillis);
			if (now.desktop)
				scale->pushInput(*now.desktop);
			if (now.cursor)
				lastCursor = std::move(now.cursor);
			if (now.cursorShape)
				lastCursorShape = std::move(now.cursorShape);

			nowTime = std::chrono::steady_clock::now();
		}

		oldTime = nowTime;
		encoder.poll();
	}

	CoUninitialize();
}
