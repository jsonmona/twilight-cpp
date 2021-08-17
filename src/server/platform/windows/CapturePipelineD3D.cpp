#include "CapturePipelineD3D.h"


CapturePipelineD3D::CapturePipelineD3D(DeviceManagerD3D _devs, int w, int h, ScaleType type) :
	log(createNamedLogger("CapturePipelineD3D")),
	capture(_devs), scale(ScaleD3D::createInstance(w, h, type)), encoder(_devs, w, h)
{
	scale->init(_devs.device, _devs.context);
}

CapturePipelineD3D::~CapturePipelineD3D() {
}

void CapturePipelineD3D::start() {
	capture.setOnNextFrame([this](CaptureData<D3D11Texture2D>&& cap) { captureNextFrame_(std::move(cap)); });
	encoder.setOnDataAvailable(writeOutput);

	capture.start(60);
	encoder.start();
}

void CapturePipelineD3D::stop() {
	encoder.stop();
	capture.stop();
}

void CapturePipelineD3D::captureNextFrame_(CaptureData<D3D11Texture2D>&& cap) {
	if (cap.desktop)
		scale->pushInput(*cap.desktop);

	CaptureData<D3D11Texture2D> data;
	data.desktop = std::make_shared<D3D11Texture2D>(scale->popOutput());
	data.cursor = std::move(cap.cursor);
	data.cursorShape = std::move(cap.cursorShape);
	encoder.pushData(std::move(data));
}
