#include "CapturePipelineD3DSoft.h"


static D3D11Texture2D intoStagedTex(const D3D11Texture2D& tex, const D3D11Device& device, const D3D11DeviceContext& context) {
	D3D11_TEXTURE2D_DESC desc;
	tex->GetDesc(&desc);

	if (desc.Usage == D3D11_USAGE_STAGING && (desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) != 0)
		return tex;

	D3D11Texture2D stageTex;
	D3D11_TEXTURE2D_DESC stageDesc = {};
	stageDesc.Format = desc.Format;
	stageDesc.Width = desc.Width;
	stageDesc.Height = desc.Height;
	stageDesc.Usage = D3D11_USAGE_STAGING;
	stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stageDesc.MipLevels = 1;
	stageDesc.ArraySize = 1;
	stageDesc.SampleDesc.Count = 1;

	device->CreateTexture2D(&stageDesc, nullptr, stageTex.data());
	context->CopyResource(stageTex.ptr(), tex.ptr());
	return stageTex;
}

static AVPixelFormat dxgi2avpixfmt(DXGI_FORMAT fmt) {
	switch (fmt) {
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		return AV_PIX_FMT_BGRA;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return AV_PIX_FMT_RGBA;
	default:
		error_quit(createNamedLogger("CapturePipelineD3DSoft"),
			"No matching AVPixelFormat for DXGI_FORMAT {}", fmt);
	}
}

static AVPixelFormat scale2avpixfmt(ScaleType type) {
	switch (type) {
	case ScaleType::AYUV:
		return AV_PIX_FMT_YUV444P;
	case ScaleType::NV12:
		return AV_PIX_FMT_YUV420P;
	default:
		error_quit(createNamedLogger("CapturePipelineD3DSoft"),
			"No matching AVPixelFormat for ScaleType {}", type);
	}
}

CapturePipelineD3DSoft::CapturePipelineD3DSoft(DeviceManagerD3D _devs, int w, int h, ScaleType type) :
	devs(_devs), capture(_devs), scale(), encoder(w, h)
{
	scale.setOutputFormat(w, h, scale2avpixfmt(type));
	encoder.setFrameRequestCallback([this]() -> CaptureData<TextureSoftware> { return _fetchTexture(); });
}

CapturePipelineD3DSoft::~CapturePipelineD3DSoft() {
}

void CapturePipelineD3DSoft::start() {
	//FIXME: Possibly unnecessary std::function copy
	encoder.setDataAvailableCallback(writeOutput);

	capture.begin();
	encoder.start();
}

void CapturePipelineD3DSoft::stop() {
	encoder.stop();
	capture.end();
}

CaptureData<TextureSoftware> CapturePipelineD3DSoft::_fetchTexture() {
	CaptureData<D3D11Texture2D> cap = capture.fetch();

	if (cap.desktop) {
		D3D11_TEXTURE2D_DESC desc;
		D3D11Texture2D tex = intoStagedTex(*cap.desktop, devs.device, devs.context);
		tex->GetDesc(&desc);

		AVPixelFormat fmt = dxgi2avpixfmt(desc.Format);
		TextureSoftware softTex = TextureSoftware::allocate(desc.Width, desc.Height, fmt);

		scale.setInputFormat(desc.Width, desc.Height, fmt);

		D3D11_MAPPED_SUBRESOURCE mapInfo;
		devs.context->Map(tex.ptr(), 0, D3D11_MAP_READ, 0, &mapInfo);

		const uint8_t* dataPtr = reinterpret_cast<uint8_t*>(mapInfo.pData);
		const int linesize = mapInfo.RowPitch;
		av_image_copy(softTex.data, softTex.linesize, &dataPtr, &linesize, fmt, desc.Width, desc.Height);

		devs.context->Unmap(tex.ptr(), 0);

		scale.pushInput(std::move(softTex));
	}

	CaptureData<TextureSoftware> ret;
	ret.desktop = std::make_shared<TextureSoftware>(scale.popOutput());
	ret.cursor = std::move(cap.cursor);
	ret.cursorShape = std::move(cap.cursorShape);
	return ret;
}