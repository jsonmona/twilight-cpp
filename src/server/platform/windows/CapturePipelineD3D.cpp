#include "CapturePipelineD3D.h"


CapturePipelineD3D::CapturePipelineD3D(DeviceManagerD3D _devs, int w, int h, ScaleType type) :
	devs(_devs), capture(_devs), scale(ScaleD3D::createInstance(w, h, type)), encoder(devs, w, h)
{
	scale->init(devs.device, devs.context);
	encoder.setFrameRequestCallback([this]() -> CaptureData<D3D11Texture2D> { return _fetchTexture(); });
}

CapturePipelineD3D::~CapturePipelineD3D() {
}

void CapturePipelineD3D::start() {
	// FIXME: Unnecessary std::function copy
	encoder.setDataAvailableCallback(writeOutput);

	capture.begin();
	encoder.start();
}

void CapturePipelineD3D::stop() {
	encoder.stop();
	capture.end();
}

CaptureData<D3D11Texture2D> CapturePipelineD3D::_fetchTexture() {
	CaptureData<D3D11Texture2D> cap = capture.fetch();
	if (cap.desktop)
		scale->pushInput(*cap.desktop);
	cap.desktop = std::make_shared<D3D11Texture2D>(scale->popOutput());
	return cap;
}
