#include "StreamManager.h"

#include <cassert>
#include <vector>
#include <utility>


static void getMainAdapterWithOutput(const DxgiFactory5& factory, DxgiAdapter1* outAdapter, DxgiOutput5* outOutput) {
	HRESULT hr;

	for (UINT i = 0; i < UINT_MAX; i++) {
		outAdapter->release();
		hr = factory->EnumAdapters1(i, outAdapter->data());
		if (FAILED(hr))
			break;

		for (UINT j = 0; j < UINT_MAX; j++) {
			DxgiOutput output;
			hr = (*outAdapter)->EnumOutputs(j, output.data());
			if (FAILED(hr))
				break;

			*outOutput = output.castTo<IDXGIOutput5>();
			if (outOutput->isValid())
				return;
		}
	}

	outAdapter->release();
	outOutput->release();
}

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
	if (!SUCCEEDED(hr)) {
		MessageBox(nullptr, L"Unable to enumerate MFTs!", nullptr, 0);
		abort();
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
	CoTaskMemFree(mftActivate);

	if (!foundTransform)
		transform.release();
	return transform;
}

StreamManager::StreamManager(decltype(videoCallback) callback, void* callbackData) :
	videoCallback(callback), videoCallbackData(callbackData)
{
	HRESULT hr;

	UINT flags = 0;
#ifndef NDEBUG
	flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
	hr = CreateDXGIFactory2(flags, dxgiFactory.guid(), (void**) dxgiFactory.data());
	if (FAILED(hr))
		abort();

	_initDevice();
	_initDuplication();
	_initEncoder();
}

void StreamManager::_initDevice() {
	HRESULT hr = S_OK;

	getMainAdapterWithOutput(dxgiFactory, &adapter, &output);
	if (adapter.isInvalid())
		abort();

	UINT flag = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#ifndef NDEBUG
	flag |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1 };
	hr = D3D11CreateDevice(adapter.ptr(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flag,
		featureLevels, 1, D3D11_SDK_VERSION, device.data(), nullptr, context.data());
	if (FAILED(hr))
		abort();

	device.castTo<ID3D10Multithread>()->SetMultithreadProtected(true);

	hr = MFCreateDXGIDeviceManager(&deviceManagerResetToken, deviceManager.data());
	if (FAILED(hr))
		abort();

	hr = deviceManager->ResetDevice(device.ptr(), deviceManagerResetToken);
	if (FAILED(hr))
		abort();

	device.castTo<ID3D10Multithread>()->SetMultithreadProtected(true);
}

void StreamManager::_initDuplication() {
	HRESULT hr;

	DXGI_FORMAT supportedFormats[] = { DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM };
	hr = output->DuplicateOutput1(device.ptr(), 0, 2, supportedFormats, outputDuplication.data());
	if (FAILED(hr))
		abort();

	//TODO: Add a timer here
	while (true) {
		DxgiResource resource;
		DXGI_OUTDUPL_FRAME_INFO frameInfo;
		hr = outputDuplication->AcquireNextFrame(1000, &frameInfo, resource.data());
		if (hr == DXGI_ERROR_WAIT_TIMEOUT)
			continue;
		if (FAILED(hr))
			abort();
		frameAcquired = true;

		D3D11_TEXTURE2D_DESC desc;
		resource.castTo<ID3D11Texture2D>()->GetDesc(&desc);
		screenWidth = desc.Width;
		screenHeight = desc.Height;
		screenFormat = desc.Format;
		break;
	}

	ColorConvD3D::Color color;
	if (screenWidth <= 720)
		color = ColorConvD3D::Color::BT601;
	else
		color = ColorConvD3D::Color::BT709;

	colorSpaceConv = ColorConvD3D::createInstance(ColorConvD3D::Type::RGB, ColorConvD3D::Type::NV12, color);
	colorSpaceConv->init(device, context);
}

void StreamManager::_initEncoder() {
	HRESULT hr;

	encoder = getVideoEncoder(deviceManager);
	if (encoder.isInvalid())
		abort();

	DWORD inputStreamCnt, outputStreamCnt;
	hr = encoder->GetStreamCount(&inputStreamCnt, &outputStreamCnt);
	if (!SUCCEEDED(hr)) {
		MessageBox(nullptr, L"Failed to get stream count!", nullptr, 0);
		abort();
	}

	encoderInputStreamId.resize(inputStreamCnt);
	encoderOutputStreamId.resize(outputStreamCnt);
	hr = encoder->GetStreamIDs(inputStreamCnt, encoderInputStreamId.data(), outputStreamCnt, encoderOutputStreamId.data());
	if (hr == E_NOTIMPL) {
		for (int i = 0; i < inputStreamCnt; i++)
			encoderInputStreamId[i] = i;
		for (int i = 0; i < outputStreamCnt; i++)
			encoderOutputStreamId[i] = i;
	}
	else if (!SUCCEEDED(hr))
		abort();

	if (inputStreamCnt < 1 || outputStreamCnt < 1) {
		MessageBox(nullptr, L"Function not implemented!\nNeed to add stream manually", nullptr, 0);
		abort();
	}

	MFMediaType mediaType;
	GUID videoFormat;

	//FIXME: Below will set stream types, but only in output->input order.
	//       While it works with NVIDIA, This might fail in other devices.

	hr = encoder->GetOutputAvailableType(encoderOutputStreamId[0], 0, mediaType.data());
	if (SUCCEEDED(hr)) {
		mediaType->SetUINT32(MF_MT_AVG_BITRATE, 15 * 1000 * 1000);  // 15Mbps
		mediaType->SetUINT32(CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
		MFSetAttributeRatio(mediaType.ptr(), MF_MT_FRAME_RATE, 60000, 1001);  // TODO: Assuming 60fps
		MFSetAttributeSize(mediaType.ptr(), MF_MT_FRAME_SIZE, screenWidth, screenHeight);
		mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlaceMode::MFVideoInterlace_Progressive);
		mediaType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile::eAVEncH264VProfile_Base);
		mediaType->SetUINT32(MF_LOW_LATENCY, 1);
		//TODO: Is there any way to find out if encoder does support low latency mode?
		//TODO: Test if using main + low latency gives nicer output
		hr = encoder->SetOutputType(encoderOutputStreamId[0], mediaType.ptr(), 0);
		if (FAILED(hr))
			abort();
	}

	GUID acceptableCodecList[] = {
		MFVideoFormat_NV12
	};

	int chosenType = -1;
	for (DWORD i = 0; i < MAXDWORD; i++) {
		mediaType.release();
		hr = encoder->GetInputAvailableType(encoderInputStreamId[0], i, mediaType.data());
		if (hr == MF_E_NO_MORE_TYPES)
			break;
		if (SUCCEEDED(hr)) {
			hr = mediaType->GetGUID(MF_MT_SUBTYPE, &videoFormat);
			if (FAILED(hr))
				continue;

			for (int j = 0; j < sizeof(acceptableCodecList) / sizeof(GUID); j++) {
				if (memcmp(&videoFormat, &acceptableCodecList[j], sizeof(GUID)) == 0) {
					chosenType = j;
					break;
				}
			}

			if (chosenType != 0) {
				hr = encoder->SetInputType(encoderInputStreamId[0], mediaType.ptr(), 0);
				if (FAILED(hr)) {
					MessageBox(nullptr, L"Failed to set input type!", nullptr, 0);
					abort();
				}
			}
		}
	}

	if (chosenType == -1)
		abort(); // no supported input type
}

void StreamManager::start() {
	flagRun.store(true, std::memory_order_release);
	captureThread = std::thread([&]() { _runCapture(); });

	encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, encoderInputStreamId[0]);
}

void StreamManager::stop() {
	flagRun.store(false, std::memory_order_release);
	encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, encoderInputStreamId[0]);
	encoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);

	captureThread.join();
}

struct EncoderExtraInfo {
	long long pts;
	long long streamClock;
};

void StreamManager::_runCapture() {
	HRESULT hr;

	bool hasTexture = false;

	bool currentCursorVisible = false;
	int currentCursorX;
	int currentCursorY;
	std::vector<uint8_t> currentCursorShape(0, 0);
	DXGI_OUTDUPL_POINTER_SHAPE_INFO currentCursorShapeInfo;

	MFMediaEventGenerator gen = encoder.castTo<IMFMediaEventGenerator>();

	// TODO: Assuming 60fps
	constexpr long long MFTime = 10000000;  // 100-ns to sec (used by MediaFoundation)
	const long long frameNum = 60000, frameDen = 1001;
	const auto startTime = std::chrono::steady_clock::now();
	long long frameCnt = 0;

	const auto calcNowFrame = [=]() {
		// Split 1,000,000,000 into two division to avoid overflowing too soon
		return (std::chrono::steady_clock::now() - startTime).count() * (frameNum / 10000) / (frameDen * 100000);
	};

	while (flagRun.load(std::memory_order_acquire)) {
		MFMediaEvent ev;
		hr = gen->GetEvent(MF_EVENT_FLAG_NO_WAIT, ev.data());
		if (hr == MF_E_NO_EVENTS_AVAILABLE) {
			Sleep(0);
			continue;
		}
		if (hr == MF_E_SHUTDOWN)
			break;
		if (FAILED(hr))
			abort();

		MediaEventType evType;
		ev->GetType(&evType);
		if (evType == METransformNeedInput) {
			while (calcNowFrame() < frameCnt)
				Sleep(1);

			do {
				if (frameAcquired) {
					hr = outputDuplication->ReleaseFrame();
					if (FAILED(hr))
						abort();
					frameAcquired = false;
				}

				DxgiResource desktopResource;
				DXGI_OUTDUPL_FRAME_INFO frameInfo;
				hr = outputDuplication->AcquireNextFrame(0, &frameInfo, desktopResource.data());
				if (SUCCEEDED(hr)) {
					frameAcquired = true;

					if (frameInfo.LastPresentTime.QuadPart != 0 || !hasTexture) {
						D3D11Texture2D rgbTex = desktopResource.castTo<ID3D11Texture2D>();
						colorSpaceConv->pushInput(rgbTex);
						hasTexture = true;
					}
					
					if (frameInfo.LastMouseUpdateTime.QuadPart != 0) {
						currentCursorVisible = frameInfo.PointerPosition.Visible;
						if (currentCursorVisible) {
							currentCursorX = frameInfo.PointerPosition.Position.x;
							currentCursorY = frameInfo.PointerPosition.Position.y;

							if (frameInfo.PointerShapeBufferSize != 0) {
								UINT pointerBufferSize = frameInfo.PointerShapeBufferSize;
								currentCursorShape.resize(pointerBufferSize);

								hr = outputDuplication->GetFramePointerShape(pointerBufferSize, currentCursorShape.data(),
									&pointerBufferSize, &currentCursorShapeInfo);
								if (FAILED(hr))
									abort();

								currentCursorShape.resize(pointerBufferSize);
							}
						}
					}
				}
				else if (hr != DXGI_ERROR_WAIT_TIMEOUT && FAILED(hr))
					abort(); //TODO: Handle DXGI_ERROR_ACCESS_LOST separately

				if (hr == DXGI_ERROR_WAIT_TIMEOUT && !hasTexture)
					Sleep(1);  //TODO: Maybe move cursor a bit to force new frame?
			} while (!hasTexture);
			// Keep try to acquire frame as long as we don't have a frame.

			frameCnt = calcNowFrame();
			const long long sampleDur = MFTime * frameDen / frameNum;
			const long long sampleTime = frameCnt * MFTime * frameDen / frameNum;

			_pushEncoderTexture(colorSpaceConv->popOutput(), sampleDur, sampleTime);
			frameCnt++;
		}
		else if (evType == METransformHaveOutput) {
			videoCallback(videoCallbackData, VideoFrame { _popEncoderData() });
		}
		else if (evType == METransformDrainComplete)
			break;
	}
}

void StreamManager::_pushEncoderTexture(const D3D11Texture2D& tex, long long sampleDur, long long sampleTime) {
	HRESULT hr;

	MFMediaBuffer mediaBuffer;
	hr = MFCreateDXGISurfaceBuffer(tex.guid(), tex.ptr(), 0, false, mediaBuffer.data());
	if (FAILED(hr))
		abort();

	MFSample sample;
	hr = MFCreateSample(sample.data());
	if (FAILED(hr))
		abort();

	sample->AddBuffer(mediaBuffer.ptr());
	sample->SetSampleDuration(sampleDur);
	sample->SetSampleTime(sampleTime);
	
	hr = encoder->ProcessInput(0, sample.ptr(), 0);
	if (hr == MF_E_NOTACCEPTING)
		return;
	if (FAILED(hr))
		abort();
}

std::vector<uint8_t> StreamManager::_popEncoderData() {
	HRESULT hr;

	MFT_OUTPUT_STREAM_INFO outputStreamInfo;
	hr = encoder->GetOutputStreamInfo(0, &outputStreamInfo);
	if (FAILED(hr))
		abort();

	bool shouldAllocateOutput = !(outputStreamInfo.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES));
	int allocSize = outputStreamInfo.cbSize + outputStreamInfo.cbAlignment * 2;
	if (shouldAllocateOutput)
		abort();  // allocating not implemented

	DWORD status;
	MFT_OUTPUT_DATA_BUFFER outputBuffer = {};
	outputBuffer.dwStreamID = encoderOutputStreamId[0];
	hr = encoder->ProcessOutput(0, 1, &outputBuffer, &status);
	if (FAILED(hr))
		abort();

	DWORD bufferCount = 0;
	hr = outputBuffer.pSample->GetBufferCount(&bufferCount);
	if (FAILED(hr))
		abort();

	DWORD totalLen = 0;
	outputBuffer.pSample->GetTotalLength(&totalLen);
	std::vector<uint8_t> dataVec(0, 0);
	dataVec.reserve(totalLen);

	for (int j = 0; j < bufferCount; j++) {
		MFMediaBuffer mediaBuffer;
		hr = outputBuffer.pSample->GetBufferByIndex(j, mediaBuffer.data());
		if (FAILED(hr))
			abort();
		BYTE* data;
		DWORD len;
		hr = mediaBuffer->Lock(&data, nullptr, &len);
		if (FAILED(hr))
			abort();

		dataVec.insert(dataVec.end(), data, data + len);

		hr = mediaBuffer->Unlock();
		if (FAILED(hr))
			abort();
	}

	outputBuffer.pSample->Release();

	if (outputBuffer.pEvents)
		outputBuffer.pEvents->Release();

	return dataVec;
}
