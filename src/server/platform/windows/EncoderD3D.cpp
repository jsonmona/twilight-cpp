#include "EncoderD3D.h"

#include "CaptureD3D.h"

#include <deque>


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
		MFT_ENUM_FLAG_SORTANDFILTER,
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

	if (mftActivate)
		CoTaskMemFree(mftActivate);

	if (!foundTransform)
		transform.release();

	return transform;
}


EncoderD3D::EncoderD3D(const std::shared_ptr<DeviceManagerD3D>& _devs, int _width, int _height) :
	devs(_devs), width(_width), height(_height),
	log(createNamedLogger("EncoderD3D"))
{
}

EncoderD3D::~EncoderD3D() {
	check_quit(runThread.joinable(), log, "Trying to destruct while thread is still running!");
}

void EncoderD3D::start() {
	_init();

	encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, inputStreamId);

	runThread = std::thread([this]() { this->_run(); });
}

void EncoderD3D::stop() {
	encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, inputStreamId);
	encoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);

	runThread.join();
}


void EncoderD3D::_init() {
	HRESULT hr;

	check_quit(!devs->isVideoSupported(), log, "Video not supported by D3D");

	encoder = getVideoEncoder(devs->mfDeviceManager);
	check_quit(encoder.isInvalid(), log, "Failed to create encoder");

	DWORD inputStreamCnt, outputStreamCnt;
	hr = encoder->GetStreamCount(&inputStreamCnt, &outputStreamCnt);
	check_quit(FAILED(hr), log, "Failed to get stream count");
	if (inputStreamCnt != 1 || outputStreamCnt != 1)
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

	colorConv = ColorConvD3D::createInstance(ColorConvD3D::Type::RGB, ColorConvD3D::Type::NV12, color);
	colorConv->init(devs->device, devs->context);
}


struct ExtraData {
	long long sampleTime;

	bool cursorVisible;
	int cursorX, cursorY;

	bool cursorShapeUpdated = false;
	std::vector<uint8_t> cursorImage;
	int cursorW, cursorH;
	float hotspotX, hotspotY;
};

void EncoderD3D::_run() {
	HRESULT hr;

	std::deque<std::shared_ptr<ExtraData>> extraData;

	std::shared_ptr<ExtraData> prevExtraData;
	bool hasTexture = false;

	MFMediaEventGenerator gen = encoder.castTo<IMFMediaEventGenerator>();

	// TODO: Assuming 60fps
	constexpr long long MFTime = 10000000;  // 100-ns to sec (used by MediaFoundation)
	const long long frameNum = 60000, frameDen = 1001;
	const auto startTime = std::chrono::steady_clock::now();
	long long frameCnt = 0;

	const auto calcNowFrame = [=]() {
		// Split 1'000'000'000 into two division to avoid overflowing too soon
		return (std::chrono::steady_clock::now() - startTime).count() * (frameNum / 10'000) / (frameDen * 100'000);
	};

	while (true) {
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
				Sleep(0);

			std::shared_ptr<ExtraData> now = std::make_shared<ExtraData>();

			_fetchTexture(now.get(), prevExtraData.get(), !hasTexture);
			hasTexture = true;

			frameCnt = calcNowFrame();
			const long long sampleDur = MFTime * frameDen / frameNum;
			const long long sampleTime = frameCnt * MFTime * frameDen / frameNum;
			now->sampleTime = sampleTime;

			extraData.push_back(now);
			prevExtraData = std::move(now);

			_pushEncoderTexture(colorConv->popOutput(), sampleDur, sampleTime);
			frameCnt++;
		}
		else if (evType == METransformHaveOutput) {
			int idx = -1;
			std::shared_ptr<ExtraData> now;
			long long sampleTime;

			EncoderDataD3D output;

			output.desktopImage = _popEncoderData(&sampleTime);
			for (int i = 0; i < extraData.size(); i++) {
				if (extraData[i]->sampleTime == sampleTime) {
					now = std::move(extraData[i]);
					idx = i;
					break;
				}
			}

			check_quit(idx == -1, log, "Failed to find matching ExtraData (size={}, sampleTime={})",
					extraData.size(), sampleTime);

			// TODO: Measure if this *optimization* is worth it
			if (idx == 0)
				extraData.pop_front();
			else
				extraData.erase(extraData.begin() + idx);

			output.cursorVisible = now->cursorVisible;
			output.cursorX = now->cursorX;
			output.cursorY = now->cursorY;

			output.cursorShapeUpdated = now->cursorShapeUpdated;
			if (now->cursorShapeUpdated) {
				output.cursorImage = now->cursorImage;
				output.cursorW = now->cursorW;
				output.cursorH = now->cursorH;
				output.hotspotX = now->hotspotX;
				output.hotspotY = now->hotspotY;
			}

			onDataAvailable(&output);
		}
		else if (evType == METransformDrainComplete)
			break;
	}
}

void EncoderD3D::_fetchTexture(ExtraData* now, const ExtraData* prev, bool forcePush) {
	CaptureDataD3D capture = onFrameRequest();

	if (capture.desktopUpdated || forcePush)
		colorConv->pushInput(capture.desktopImage);

	if (capture.cursorUpdated) {
		now->cursorVisible = capture.cursorVisible;
		now->cursorX = capture.cursorX;
		now->cursorY = capture.cursorY;
	}
	else {
		now->cursorVisible = prev->cursorVisible;
		now->cursorX = prev->cursorX;
		now->cursorY = prev->cursorY;
	}

	if (capture.cursorShapeUpdated) {
		now->cursorShapeUpdated = true;
		now->cursorImage = std::move(capture.cursorImage);
		now->cursorW = capture.cursorW;
		now->cursorH = capture.cursorH;
		now->hotspotX = capture.hotspotX;
		now->hotspotY = capture.hotspotY;
	}
	else
		now->cursorShapeUpdated = false;
}

void EncoderD3D::_pushEncoderTexture(const D3D11Texture2D& tex, long long sampleDur, long long sampleTime) {
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

std::vector<uint8_t> EncoderD3D::_popEncoderData(long long* sampleTime) {
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

	if (sampleTime)
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