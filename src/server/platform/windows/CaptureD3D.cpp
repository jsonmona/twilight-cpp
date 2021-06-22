#include "CaptureD3D.h"

#include <cassert>
#include <deque>
#include <utility>


static MFTransform getVideoEncoder(const MFDxgiDeviceManager& deviceManager) {
	HRESULT hr;
	MFTransform transform;
	IMFActivate** mftActivate;
	UINT32 arraySize;

	MFT_REGISTER_TYPE_INFO outputType = {};
	outputType.guidMajorType = MFMediaType_Video;
	outputType.guidSubtype = MFVideoFormat_H264;

	hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
		MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_LOCALMFT |
		MFT_ENUM_FLAG_TRANSCODE_ONLY | MFT_ENUM_FLAG_SORTANDFILTER,
		nullptr, &outputType,
		&mftActivate, &arraySize);
	
	if (FAILED(hr)) {
		mftActivate = nullptr;
		arraySize = 0;
	}

	bool foundTransform = false;

	for (unsigned i = 0; i < arraySize; i++) {
		if (!foundTransform) {
			transform.release();
			hr = mftActivate[i]->ActivateObject(transform.guid(), (void**)transform.data());
			if (FAILED(hr))
				continue;

			MFAttributes attr;
			hr = transform->GetAttributes(attr.data());
			if (FAILED(hr))
				continue;

			UINT32 flagD3DAware = 0;
			attr->GetUINT32(MF_SA_D3D11_AWARE, &flagD3DAware);
			if (flagD3DAware == 0)
				continue;

			UINT32 flagAsync = 0;
			attr->GetUINT32(MF_TRANSFORM_ASYNC, &flagAsync);
			if (flagAsync == 0)
				continue;

			hr = attr->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, 1);
			if (FAILED(hr))
				continue;

			hr = transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)deviceManager.ptr());
			if (FAILED(hr))
				continue;

			foundTransform = true;
		}
		mftActivate[i]->Release();
	}

	if(mftActivate)
		CoTaskMemFree(mftActivate);

	if (!foundTransform)
		transform.release();

	return transform;
}

CaptureD3D::CaptureD3D(const decltype(devs)& _deviceManager) :
	log(createNamedLogger("CaptureD3D")), devs(_deviceManager)
{
}

void CaptureD3D::begin() {
	HRESULT hr;

	frameAcquired = false;
	outputDuplication.release();

	DXGI_FORMAT supportedFormats[] = { DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM };
	hr = devs->output->DuplicateOutput1(devs->device.ptr(), 0, 2, supportedFormats, outputDuplication.data());
	check_quit(FAILED(hr), log, "Failed to duplicate output");
}

void CaptureD3D::end() {
	HRESULT hr;

	if (frameAcquired) {
		frameAcquired = false;

		hr = outputDuplication->ReleaseFrame();
		check_quit(FAILED(hr), log, "Failed to release frame ({:#x})", hr);
	}

	outputDuplication.release();
}

CaptureDataD3D CaptureD3D::fetch() {
	HRESULT hr;
	CaptureDataD3D info;

	if (frameAcquired) {
		hr = outputDuplication->ReleaseFrame();
		check_quit(FAILED(hr), log, "Failed to release frame ({})", hr);
		frameAcquired = false;
	}

	DxgiResource desktopResource;
	DXGI_OUTDUPL_FRAME_INFO frameInfo;
	hr = outputDuplication->AcquireNextFrame(0, &frameInfo, desktopResource.data());
	if (SUCCEEDED(hr)) {
		frameAcquired = true;

		// always update desktop image
		info.desktopImage = desktopResource.castTo<ID3D11Texture2D>();
		if (frameInfo.LastPresentTime.QuadPart != 0) {
			info.desktopUpdated = true;
		}

		if (frameInfo.LastMouseUpdateTime.QuadPart != 0) {
			info.cursorUpdated = true;
			info.cursorVisible = frameInfo.PointerPosition.Visible;
			if (info.cursorVisible) {
				info.cursorX = frameInfo.PointerPosition.Position.x;
				info.cursorY = frameInfo.PointerPosition.Position.y;
			}
		}

		if (frameInfo.PointerShapeBufferSize != 0) {
			info.cursorShapeUpdated = true;

			UINT bufferSize = frameInfo.PointerShapeBufferSize;
			std::vector<uint8_t> buffer(bufferSize);

			DXGI_OUTDUPL_POINTER_SHAPE_INFO cursorInfo;
			hr = outputDuplication->GetFramePointerShape(bufferSize, buffer.data(),
				&bufferSize, &cursorInfo);
			check_quit(FAILED(hr), log, "Failed to fetch frame pointer shape");

			info.hotspotX = cursorInfo.HotSpot.x;
			info.hotspotY = cursorInfo.HotSpot.y;
			info.cursorImage.resize(cursorInfo.Height * cursorInfo.Width * 4);
			if (cursorInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
				info.cursorW = cursorInfo.Width;
				info.cursorH = cursorInfo.Height;

				for (int i = 0; i < cursorInfo.Height; i++) {
					for (int j = 0; j < cursorInfo.Width; j++) {
						// bgra -> rgba
						uint32_t val = *reinterpret_cast<uint32_t*>(buffer.data() + (i * cursorInfo.Pitch + j * 4));
						val = ((val & 0x00FF00FF) << 16) | ((val & 0x00FF00FF) >> 16) | (val & 0xFF00FF00);
						*reinterpret_cast<uint32_t*>(info.cursorImage.data() + (i * cursorInfo.Width * 4 + j * 4)) = val;
					}
				}
			}
			else if (cursorInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME) {
				info.cursorW = cursorInfo.Width;
				info.cursorH = cursorInfo.Height / 2;

				for (int i = 0; i < cursorInfo.Height / 2; i++) {
					for (int j = 0; j < cursorInfo.Width / 8; j++) {
						uint8_t value = buffer[i * cursorInfo.Pitch + j];
						uint8_t alpha = buffer[(i + cursorInfo.Height / 2) * cursorInfo.Pitch + j];
						for (int k = 0; k < 8; k++) {
							uint8_t rgbValue = (value & 1) ? 0xFF : 0x00;
							uint8_t alphaValue = (alpha & 1) ? 0xFF : 0x00;
							value >>= 1;
							alpha >>= 1;

							info.cursorImage[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4] = rgbValue;
							info.cursorImage[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4 + 1] = rgbValue;
							info.cursorImage[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4 + 2] = rgbValue;
							info.cursorImage[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4 + 3] = alphaValue;
						}
					}
				}
			}
			else {
				log->warn("Unknown cursor type: {}", cursorInfo.Type);
				info.cursorW = 0;
				info.cursorH = 0;
				info.cursorImage.resize(0);
			}
		}
	}
	else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
		// ignore
	}
	else if (hr == DXGI_ERROR_ACCESS_LOST)
		error_quit(log, "Needs to recreate output duplication");
	else
		error_quit(log, "Failed to acquire next frame ({})", hr);

	return info;
}
