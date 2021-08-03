#include "CapturePipelineD3D.h"


CapturePipelineD3D::CapturePipelineD3D(DeviceManagerD3D _devs, int w, int h, ScaleType type) :
	log(createNamedLogger("CapturePipelineD3D")),
	devs(_devs), capture(_devs), scale(ScaleD3D::createInstance(w, h, type)), encoder(devs, w, h)
{
	scale->init(devs.device, devs.context);
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

	bool firstFrameReceived = false;

	const double maxDriftRatio = 0.0001;
	const int maxPrevTicks = 60;
	const std::chrono::nanoseconds encodeTicks = std::chrono::nanoseconds(1'000'000'000 / 60);

	auto oldTime = std::chrono::steady_clock::now();

	while (flagRun.load(std::memory_order_acquire)) {
		/* poll capture */ {
			firstFrameReceived = true;

			auto now = capture.poll();
			if (now.desktop)
				scale->pushInput(*now.desktop);
			if (now.cursor)
				lastCursor = std::move(now.cursor);
			if (now.cursorShape)
				lastCursorShape = std::move(now.cursorShape);
		}

		auto nowTime = std::chrono::steady_clock::now();
		if (firstFrameReceived && (nowTime - oldTime) >= encodeTicks) {
			oldTime = nowTime;
			encoder.poll();
		}
	}

	CoUninitialize();
}
