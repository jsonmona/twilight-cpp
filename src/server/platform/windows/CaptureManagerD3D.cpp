#include "CaptureManagerD3D.h"

#include <cassert>
#include <deque>
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

CaptureManagerD3D::CaptureManagerD3D(decltype(videoCallback) callback, void* callbackData) :
	log(createNamedLogger("CaptureManagerD3D")),
	videoCallback(callback), videoCallbackData(callbackData)
{
	HRESULT hr;

	UINT flags = 0;
#ifndef NDEBUG
	flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
	hr = CreateDXGIFactory2(flags, dxgiFactory.guid(), (void**) dxgiFactory.data());
	check_quit(FAILED(hr), log, "Failed to create DXGI factory 5");

	//TODO: Make this a proper parameter (set by client)
	width = 1920;
	height = 1080;

	_initDevice();
	_initEncoder();
	_initDuplication();
}

void CaptureManagerD3D::_initDevice() {
	HRESULT hr;

	getMainAdapterWithOutput(dxgiFactory, &adapter, &output);
	check_quit(adapter.isInvalid(), log, "Failed to find main adapter and output");

	UINT flag = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
#ifndef NDEBUG
	flag |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1 };
	hr = D3D11CreateDevice(adapter.ptr(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flag,
		featureLevels, 1, D3D11_SDK_VERSION, device.data(), nullptr, context.data());
	check_quit(FAILED(hr), log, "Failed to create D3D11 device");

	device.castTo<ID3D10Multithread>()->SetMultithreadProtected(true);

	hr = MFCreateDXGIDeviceManager(&deviceManagerResetToken, deviceManager.data());
	check_quit(FAILED(hr), log, "Failed to create Media Foundation DXGI device manager");

	hr = deviceManager->ResetDevice(device.ptr(), deviceManagerResetToken);
	check_quit(FAILED(hr), log, "Failed to reset device");

	device.castTo<ID3D10Multithread>()->SetMultithreadProtected(true);
}

void CaptureManagerD3D::_initEncoder() {
	HRESULT hr;

	encoder = getVideoEncoder(deviceManager);
	check_quit(encoder.isInvalid(), log, "Failed to create encoder");

	DWORD inputStreamCnt, outputStreamCnt;
	hr = encoder->GetStreamCount(&inputStreamCnt, &outputStreamCnt);
	check_quit(FAILED(hr), log, "Failed to get stream count");
	if(inputStreamCnt != 1 || outputStreamCnt != 1)
		error_quit(log, "Invalid number of stream: input={} output={}", inputStreamCnt, outputStreamCnt);

	hr = encoder->GetStreamIDs(1, &inputStreamId, 1, &outputStreamId);
	if (hr == E_NOTIMPL) {
		inputStreamId = 0;
		outputStreamId = 0;
	}
	check_quit(FAILED(hr), log, "Failed to duplicate output");

	if (inputStreamCnt < 1 || outputStreamCnt < 1)
		error_quit(log, "Adding stream manually is not implemented");

	MFMediaType mediaType;
	GUID videoFormat;

	//FIXME: Below will set stream types, but only in output->input order.
	//       While it works with NVIDIA, This might fail in other devices.

	hr = encoder->GetOutputAvailableType(outputStreamId, 0, mediaType.data());
	if (SUCCEEDED(hr)) {
		mediaType->SetUINT32(MF_MT_AVG_BITRATE, 15 * 1000 * 1000);  // 15Mbps
		mediaType->SetUINT32(CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
		MFSetAttributeRatio(mediaType.ptr(), MF_MT_FRAME_RATE, 60000, 1001);  // TODO: Assuming 60fps
		MFSetAttributeSize(mediaType.ptr(), MF_MT_FRAME_SIZE, width, height);
		mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlaceMode::MFVideoInterlace_Progressive);
		mediaType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile::eAVEncH264VProfile_Base);
		mediaType->SetUINT32(MF_LOW_LATENCY, 1);
		//TODO: Is there any way to find out if encoder DOES support low latency mode?
		//TODO: Test if using main + low latency gives nicer output
		hr = encoder->SetOutputType(outputStreamId, mediaType.ptr(), 0);
		check_quit(FAILED(hr), log, "Failed to set output type");
	}

	GUID acceptableCodecList[] = {
		MFVideoFormat_NV12
	};

	int chosenType = -1;
	for (DWORD i = 0; i < MAXDWORD; i++) {
		mediaType.release();
		hr = encoder->GetInputAvailableType(inputStreamId, i, mediaType.data());
		if (hr == MF_E_NO_MORE_TYPES)
			break;
		if (SUCCEEDED(hr)) {
			hr = mediaType->GetGUID(MF_MT_SUBTYPE, &videoFormat);
			check_quit(FAILED(hr), log, "Failed to query input type");

			for (int j = 0; j < sizeof(acceptableCodecList) / sizeof(GUID); j++) {
				if (memcmp(&videoFormat, &acceptableCodecList[j], sizeof(GUID)) == 0) {
					chosenType = j;
					break;
				}
			}

			if (chosenType != 0) {
				hr = encoder->SetInputType(inputStreamId, mediaType.ptr(), 0);
				check_quit(FAILED(hr), log, "Failed to set input type");
			}
		}
	}

	check_quit(chosenType == -1, log, "No supported input type found");

	ColorConvD3D::Color color;
	if (width <= 720)
		color = ColorConvD3D::Color::BT601;
	else
		color = ColorConvD3D::Color::BT709;

	colorSpaceConv = ColorConvD3D::createInstance(ColorConvD3D::Type::RGB, ColorConvD3D::Type::NV12, color);
	colorSpaceConv->init(device, context);
}

void CaptureManagerD3D::_initDuplication() {
	HRESULT hr;

	frameAcquired = false;

	DXGI_FORMAT supportedFormats[] = { DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM };
	hr = output->DuplicateOutput1(device.ptr(), 0, 2, supportedFormats, outputDuplication.data());
	check_quit(FAILED(hr), log, "Failed to duplicate output");
}

void CaptureManagerD3D::start() {
	flagRun.store(true, std::memory_order_release);
	captureThread = std::thread([&]() { _runCapture(); });

	encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, inputStreamId);
}

void CaptureManagerD3D::stop() {
	flagRun.store(false, std::memory_order_release);
	encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, inputStreamId);
	encoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);

	captureThread.join();
}


struct ExtraData {
	long long sampleTime = -1;
	int cursorX = 0, cursorY = 0;
	bool cursorVisible = false;
	bool cursorShapeUpdated = false;
	int cursorWidth = 0, cursorHeight = 0;
	int cursorHotspotX = 0, cursorHotspotY = 0;
	std::vector<uint8_t> cursorShape;
};


bool CaptureManagerD3D::_fetchTexture(ExtraData* now, const ExtraData* prev, bool forcePush) {
	HRESULT hr;

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

		if (frameInfo.LastPresentTime.QuadPart != 0 || forcePush) {
			D3D11Texture2D rgbTex = desktopResource.castTo<ID3D11Texture2D>();
			colorSpaceConv->pushInput(rgbTex);
		}

		if (frameInfo.LastMouseUpdateTime.QuadPart != 0) {
			now->cursorVisible = frameInfo.PointerPosition.Visible;
			if (now->cursorVisible) {
				now->cursorX = frameInfo.PointerPosition.Position.x;
				now->cursorY = frameInfo.PointerPosition.Position.y;

				if (frameInfo.PointerShapeBufferSize != 0) {
					now->cursorShapeUpdated = true;

					UINT bufferSize = frameInfo.PointerShapeBufferSize;
					std::vector<uint8_t> buffer(bufferSize);

					DXGI_OUTDUPL_POINTER_SHAPE_INFO cursorInfo;
					hr = outputDuplication->GetFramePointerShape(bufferSize, buffer.data(),
						&bufferSize, &cursorInfo);
					check_quit(FAILED(hr), log, "Failed to fetch frame pointer shape");

					now->cursorHotspotX = cursorInfo.HotSpot.x;
					now->cursorHotspotY = cursorInfo.HotSpot.y;
					now->cursorShape.resize(cursorInfo.Height * cursorInfo.Width * 4);
					if (cursorInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
						now->cursorWidth = cursorInfo.Width;
						now->cursorHeight = cursorInfo.Height;

						for (int i = 0; i < cursorInfo.Height; i++) {
							for (int j = 0; j < cursorInfo.Width; j++) {
								// bgra -> rgba
								uint32_t val = *reinterpret_cast<uint32_t*>(buffer.data() + (i * cursorInfo.Pitch + j * 4));
								val = ((val & 0x00FF00FF) << 16) | ((val & 0x00FF00FF) >> 16) | (val & 0xFF00FF00);
								*reinterpret_cast<uint32_t*>(now->cursorShape.data() + (i * cursorInfo.Width * 4 + j * 4)) = val;
							}
						}
					}
					else if (cursorInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME) {
						now->cursorWidth = cursorInfo.Width;
						now->cursorHeight = cursorInfo.Height / 2;

						for (int i = 0; i < cursorInfo.Height / 2; i++) {
							for (int j = 0; j < cursorInfo.Width / 8; j++) {
								uint8_t value = buffer[i * cursorInfo.Pitch + j];
								uint8_t alpha = buffer[(i + cursorInfo.Height / 2) * cursorInfo.Pitch + j];
								for (int k = 0; k < 8; k++) {
									uint8_t rgbValue = (value & 1) ? 0xFF : 0x00;
									uint8_t alphaValue = (alpha & 1) ? 0xFF : 0x00;
									value >>= 1;
									alpha >>= 1;

									now->cursorShape[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4] = rgbValue;
									now->cursorShape[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4 + 1] = rgbValue;
									now->cursorShape[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4 + 2] = rgbValue;
									now->cursorShape[i * cursorInfo.Width * 4 + j * 8 * 4 + k * 4 + 3] = alphaValue;
								}
							}
						}
					}
					else {
						log->warn("Unknown cursor type: {}", cursorInfo.Type);
					}
				}
			}
		}
		else if (prev) {
			now->cursorVisible = prev->cursorVisible;
			now->cursorX = prev->cursorX;
			now->cursorY = prev->cursorY;
		}
	}
	else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
		now->cursorVisible = prev->cursorVisible;
		now->cursorX = prev->cursorX;
		now->cursorY = prev->cursorY;
		return false;
	}
	else if (hr == DXGI_ERROR_ACCESS_LOST)
		error_quit(log, "Needs to recreate output duplication");
	else
		error_quit(log, "Failed to acquire next frame ({})", hr);

	return true;
}

void CaptureManagerD3D::_runCapture() {
	HRESULT hr;

	std::deque<std::shared_ptr<ExtraData>> extraData;

	std::shared_ptr<ExtraData> prevExtraData;
	bool frameAcquired = false;
	bool hasTexture = false;

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
		check_quit(FAILED(hr), log, "Failed to get next event ({})", hr);

		MediaEventType evType;
		ev->GetType(&evType);
		if (evType == METransformNeedInput) {
			while (calcNowFrame() < frameCnt)
				Sleep(1);

			std::shared_ptr<ExtraData> now = std::make_shared<ExtraData>();

			if (hasTexture)
				_fetchTexture(now.get(), prevExtraData.get(), false);
			else {
				while (true) {
					hasTexture = _fetchTexture(now.get(), prevExtraData.get(), true);
					if (hasTexture)
						break;
					Sleep(1);
				}
			}

			frameCnt = calcNowFrame();
			const long long sampleDur = MFTime * frameDen / frameNum;
			const long long sampleTime = frameCnt * MFTime * frameDen / frameNum;
			now->sampleTime = sampleTime;

			extraData.push_back(now);
			prevExtraData = std::move(now);

			_pushEncoderTexture(colorSpaceConv->popOutput(), sampleDur, sampleTime);
			frameCnt++;
		}
		else if (evType == METransformHaveOutput) {
			int idx = -1;
			std::shared_ptr<ExtraData> now;
			long long sampleTime;

			VideoInfo video = {};
			video.desktopImage = _popEncoderData(&sampleTime);
			for (int i = 0; i < extraData.size(); i++) {
				if (extraData[i]->sampleTime == sampleTime) {
					now = std::move(extraData[i]);
					idx = i;
					break;
				}
			}

			check_quit(idx == -1, log, "Failed to find matching ExtraData (size={}, sampleTime={})", extraData.size(), sampleTime);

			// TODO: Measure if this *optimization* is worth it
			if (idx == 0)
				extraData.pop_front();
			else
				extraData.erase(extraData.begin() + idx);

			video.cursorVisible = now->cursorVisible;
			video.cursorPosX = now->cursorX;
			video.cursorPosY = now->cursorY;

			CursorInfo cursor = {};
			if (now->cursorShapeUpdated) {
				cursor.cursorImage = now->cursorShape;
				cursor.width = now->cursorWidth;
				cursor.height = now->cursorHeight;
				cursor.hotspotX = now->cursorHotspotX;
				cursor.hotspotY = now->cursorHotspotY;
			}

			if(now->cursorShapeUpdated)
				videoCallback(videoCallbackData, &video, &cursor);
			else
				videoCallback(videoCallbackData, &video, nullptr);
		}
		else if (evType == METransformDrainComplete)
			break;
	}

	if (frameAcquired) {
		hr = outputDuplication->ReleaseFrame();
		check_quit(FAILED(hr), log, "Failed to release frame ({})", hr);
		frameAcquired = false;
	}
}

void CaptureManagerD3D::_pushEncoderTexture(const D3D11Texture2D& tex, long long sampleDur, long long sampleTime) {
	HRESULT hr;

	MFMediaBuffer mediaBuffer;
	hr = MFCreateDXGISurfaceBuffer(tex.guid(), tex.ptr(), 0, false, mediaBuffer.data());
	check_quit(FAILED(hr), log, "Failed to create media buffer containing D3D11 texture");

	MFSample sample;
	hr = MFCreateSample(sample.data());
	check_quit(FAILED(hr), log, "Failed to create a sample");

	sample->AddBuffer(mediaBuffer.ptr());
	sample->SetSampleDuration(sampleDur);
	sample->SetSampleTime(sampleTime);
	
	hr = encoder->ProcessInput(0, sample.ptr(), 0);
	if (hr == MF_E_NOTACCEPTING)
		return;
	check_quit(FAILED(hr), log, "Failed to put input into encoder");
}

std::vector<uint8_t> CaptureManagerD3D::_popEncoderData(long long* sampleTime) {
	HRESULT hr;

	MFT_OUTPUT_STREAM_INFO outputStreamInfo;
	hr = encoder->GetOutputStreamInfo(0, &outputStreamInfo);
	check_quit(FAILED(hr), log, "Failed to get output stream info");

	bool shouldAllocateOutput = !(outputStreamInfo.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES));
	int allocSize = outputStreamInfo.cbSize + outputStreamInfo.cbAlignment * 2;
	check_quit(shouldAllocateOutput, log, "Allocating output is not implemented yet");

	DWORD status;
	MFT_OUTPUT_DATA_BUFFER outputBuffer = {};
	outputBuffer.dwStreamID = outputStreamId;
	hr = encoder->ProcessOutput(0, 1, &outputBuffer, &status);
	check_quit(FAILED(hr), log, "Failed to retrieve output from encoder");

	if(sampleTime != nullptr)
		outputBuffer.pSample->GetSampleTime(sampleTime);

	DWORD bufferCount = 0;
	hr = outputBuffer.pSample->GetBufferCount(&bufferCount);
	check_quit(FAILED(hr), log, "Failed to get buffer count");

	DWORD totalLen = 0;
	hr = outputBuffer.pSample->GetTotalLength(&totalLen);
	check_quit(FAILED(hr), log, "Failed to query total length of sample");

	std::vector<uint8_t> dataVec(0, 0);
	dataVec.reserve(totalLen);

	for (int j = 0; j < bufferCount; j++) {
		MFMediaBuffer mediaBuffer;
		hr = outputBuffer.pSample->GetBufferByIndex(j, mediaBuffer.data());
		check_quit(FAILED(hr), log, "Failed to get buffer");

		BYTE* data;
		DWORD len;
		hr = mediaBuffer->Lock(&data, nullptr, &len);
		check_quit(FAILED(hr), log, "Failed to lock buffer");

		dataVec.insert(dataVec.end(), data, data + len);

		hr = mediaBuffer->Unlock();
		check_quit(FAILED(hr), log, "Failed to unlock buffer");
	}

	outputBuffer.pSample->Release();

	if (outputBuffer.pEvents)
		outputBuffer.pEvents->Release();

	return dataVec;
}
