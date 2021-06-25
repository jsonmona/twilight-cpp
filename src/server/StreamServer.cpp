#include "StreamServer.h"

/*
static void loadd3d() {
	D3D11_TEXTURE2D_DESC desc;
	data.desktopImage->GetDesc(&desc);

	D3D11Texture2D stageTex;

	if (desc.Usage == D3D11_USAGE_STAGING) {
		stageTex = data.desktopImage;
	}
	else {
		D3D11_TEXTURE2D_DESC stageDesc = {};
		stageDesc.Format = desc.Format;
		stageDesc.Width = desc.Width;
		stageDesc.Height = desc.Height;
		stageDesc.Usage = D3D11_USAGE_STAGING;
		stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stageDesc.MipLevels = 1;
		stageDesc.ArraySize = 1;
		stageDesc.SampleDesc.Count = 1;

		devs->device->CreateTexture2D(&stageDesc, nullptr, stageTex.data());
		devs->context->CopyResource(stageTex.ptr(), data.desktopImage.ptr());
	}

	if (inputWidth != desc.Width || inputHeight != desc.Height || inputFormat != desc.Format) {
		inputWidth = desc.Width;
		inputHeight = desc.Height;
		inputFormat = desc.Format;

		sws_freeContext(swsCtx);

		AVPixelFormat inputAvFormat = intoAVPixelFormat(inputFormat);
		check_quit(inputAvFormat == AV_PIX_FMT_NONE, log,
			"Failed to find matching AVFormat for DXGI_FORMAT {}", inputFormat);

		swsCtx = sws_getContext(inputWidth, inputHeight, inputAvFormat,
			width, height, AV_PIX_FMT_YUV420P,
			SWS_BICUBIC, nullptr, nullptr, nullptr);
		check_quit(swsCtx == nullptr, log, "Failed to create swscale context");
	}

	D3D11_MAPPED_SUBRESOURCE mapInfo;
	devs->context->Map(stageTex.ptr(), 0, D3D11_MAP_READ, 0, &mapInfo);

	const uint8_t* srcData = reinterpret_cast<uint8_t*>(mapInfo.pData);
	int srcStride = mapInfo.RowPitch;
	sws_scale(swsCtx, &srcData, &srcStride, 0, inputHeight, frameData.data(), frameLinesize.data());

	devs->context->Unmap(stageTex.ptr(), 0);
}
*/