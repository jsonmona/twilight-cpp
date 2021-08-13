#include "CaptureD3D.h"

#include "common/platform/windows/winheaders.h"

#include <cassert>
#include <deque>
#include <utility>


CaptureD3D::CaptureD3D(DeviceManagerD3D _devs) :
	log(createNamedLogger("CaptureD3D")),
	output(_devs.output), device(_devs.device)
{
}

CaptureD3D::~CaptureD3D() {
}

void CaptureD3D::start() {
	HRESULT hr;

	check_quit(outputDuplication.isValid(), log, "Started without stopping");

	timeBeginPeriod(1);

	firstFrameSent = false;
	openDuplication_();
}

void CaptureD3D::stop() {
	if (frameAcquired) {
		HRESULT hr = outputDuplication->ReleaseFrame();
		if (hr != DXGI_ERROR_ACCESS_LOST)
			check_quit(FAILED(hr), log, "Failed to release frame ({})", hr);
		frameAcquired = false;
	}

	outputDuplication.release();

	timeEndPeriod(1);
}

CaptureData<D3D11Texture2D> CaptureD3D::poll(std::chrono::milliseconds awaitTime) {
	HRESULT hr;
	CaptureData<D3D11Texture2D> cap;

	// Access denied (secure desktop, etc.)
	//TODO: Show black screen instead of last image
	if (outputDuplication.isInvalid()) {
		openDuplication_();
		if (outputDuplication.isInvalid())
			return cap;
	}

	if (frameAcquired) {
		hr = outputDuplication->ReleaseFrame();
		if (hr == DXGI_ERROR_ACCESS_LOST)
			openDuplication_();
		else
			check_quit(FAILED(hr), log, "Failed to release frame ({})", hr);
		frameAcquired = false;
	}

	// Access denied (secure desktop, etc.)
	//TODO: Show black screen instead of last image
	//FIXME: And this code block is duplicate of above
	if (outputDuplication.isInvalid()) {
		openDuplication_();
		if (outputDuplication.isInvalid())
			return cap;
	}

	UINT timeoutMillis = std::min<long long>(std::numeric_limits<UINT>::max(), std::max<long long>(0, awaitTime.count()));
	DxgiResource desktopResource;
	DXGI_OUTDUPL_FRAME_INFO frameInfo;
	hr = outputDuplication->AcquireNextFrame(timeoutMillis, &frameInfo, desktopResource.data());
	if (SUCCEEDED(hr)) {
		frameAcquired = true;

		if (frameInfo.LastPresentTime.QuadPart != 0 || !firstFrameSent) {
			firstFrameSent = true;
			D3D11Texture2D rgbTex = desktopResource.castTo<ID3D11Texture2D>();
			cap.desktop = std::make_shared<D3D11Texture2D>(std::move(rgbTex));
		}

		if (frameInfo.LastMouseUpdateTime.QuadPart != 0) {
			cap.cursor = std::make_shared<CursorData>();
			cap.cursor->visible = frameInfo.PointerPosition.Visible;
			if (frameInfo.PointerPosition.Visible) {
				cap.cursor->posX = frameInfo.PointerPosition.Position.x;
				cap.cursor->posY = frameInfo.PointerPosition.Position.y;
			}
		}

		if (frameInfo.PointerShapeBufferSize != 0) {
			cap.cursorShape = std::make_shared<CursorShapeData>();

			UINT bufferSize = frameInfo.PointerShapeBufferSize;
			std::vector<uint8_t> buffer(bufferSize);

			DXGI_OUTDUPL_POINTER_SHAPE_INFO cursorInfo;
			hr = outputDuplication->GetFramePointerShape(bufferSize, buffer.data(),
				&bufferSize, &cursorInfo);
			check_quit(FAILED(hr), log, "Failed to fetch frame pointer shape");

			cap.cursorShape->hotspotX = cursorInfo.HotSpot.x;
			cap.cursorShape->hotspotY = cursorInfo.HotSpot.y;
			parseCursor_(cap.cursorShape.get(), cursorInfo, buffer);
		}
	}
	else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
		// ignore
	}
	else if (hr == DXGI_ERROR_ACCESS_LOST)
		openDuplication_();
	else
		error_quit(log, "Failed to acquire next frame ({})", hr);

	return cap;
}

void CaptureD3D::openDuplication_() {
	HRESULT hr;

	if (outputDuplication.isValid())
		outputDuplication.release();

	frameAcquired = false;

	DXGI_FORMAT supportedFormats[] = { DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM };
	hr = output->DuplicateOutput1(device.ptr(), 0, 2, supportedFormats, outputDuplication.data());
	if (hr == E_ACCESSDENIED)
		Sleep(1);
	else
		check_quit(FAILED(hr), log, "Failed to duplicate output ({:#x})", hr);
}

void CaptureD3D::parseCursor_(CursorShapeData* cursorShape, const DXGI_OUTDUPL_POINTER_SHAPE_INFO& cursorInfo, const std::vector<uint8_t>& buffer) {
	if (cursorInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
		cursorShape->image.resize(cursorInfo.Height * cursorInfo.Width * 4);
		cursorShape->width = cursorInfo.Width;
		cursorShape->height = cursorInfo.Height;

		uint8_t* const basePtr = cursorShape->image.data();
		for (int i = 0; i < cursorInfo.Height; i++) {
			for (int j = 0; j < cursorInfo.Width; j++) {
				// bgra -> rgba
				uint32_t val = *reinterpret_cast<const uint32_t*>(buffer.data() + (i * cursorInfo.Pitch + j * 4));
				val = ((val & 0x00FF00FF) << 16) | ((val & 0x00FF00FF) >> 16) | (val & 0xFF00FF00);
				*reinterpret_cast<uint32_t*>(basePtr + (i * cursorInfo.Width * 4 + j * 4)) = val;
			}
		}
	}
	else if (cursorInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME) {
		cursorShape->image.resize(cursorInfo.Height * cursorInfo.Width * 4 / 2);
		cursorShape->width = cursorInfo.Width;
		cursorShape->height = cursorInfo.Height / 2;

		uint8_t* const basePtr = cursorShape->image.data();
		for (int i = 0; i < cursorInfo.Height / 2; i++) {
			for (int j = 0; j < cursorInfo.Width / 8; j++) {
				uint8_t value = buffer[i * cursorInfo.Pitch + j];
				uint8_t alpha = buffer[(i + cursorInfo.Height / 2) * cursorInfo.Pitch + j];
				for (int k = 0; k < 8; k++) {
					uint8_t rgbValue = (value & 1) ? 0xFF : 0x00;
					uint8_t alphaValue = (alpha & 1) ? 0xFF : 0x00;
					value >>= 1;
					alpha >>= 1;

					basePtr[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4] = rgbValue;
					basePtr[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4 + 1] = rgbValue;
					basePtr[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4 + 2] = rgbValue;
					basePtr[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4 + 3] = alphaValue;
				}
			}
		}
	}
	else {
		log->warn("Unknown cursor type: {}", cursorInfo.Type);
		cursorShape->image.resize(0);
		cursorShape->height = 0;
		cursorShape->width = 0;
	}
}
