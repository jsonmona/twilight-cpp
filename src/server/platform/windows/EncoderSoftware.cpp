#include "EncoderSoftware.h"

#include "EncoderD3D.h"
#include "CaptureD3D.h"

#include <deque>


struct ExtraData {
	long long sampleTime;

	bool cursorUpdated;
	bool cursorVisible;
	int cursorX, cursorY;

	bool cursorShapeUpdated = false;
	std::vector<uint8_t> cursorImage;
	int cursorW, cursorH;
	float hotspotX, hotspotY;
};


static AVPixelFormat intoAVPixelFormat(DXGI_FORMAT dxgi) {
	switch (dxgi) {
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		return AV_PIX_FMT_BGRA;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return AV_PIX_FMT_RGBA;
	default:
		return AV_PIX_FMT_NONE;
	}
}

// Aligns value into next multiple of x
static int align_multiple(int value, int x) {
	if (value % x == 0)
		return value;
	return value + (x - value % x);
}


EncoderSoftware::EncoderSoftware(const std::shared_ptr<DeviceManagerD3D>& _devs, int _width, int _height) :
	log(createNamedLogger("EncoderSoftware")),
	devs(_devs), width(_width), height(_height)
{
}

EncoderSoftware::~EncoderSoftware() {
	if (encoderCtx)
		avcodec_free_context(&encoderCtx);

	if (swsCtx) {
		sws_freeContext(swsCtx);
		swsCtx = nullptr;
	}

	if (frameData[0]) {
		av_free(frameData[0]);
		std::fill(frameData.begin(), frameData.end(), nullptr);
	}
}

void EncoderSoftware::start() {
	int stat;

	inputWidth = -1;
	inputHeight = -1;
	inputFormat = DXGI_FORMAT_UNKNOWN;

	stat = av_image_alloc(frameData.data(), frameLinesize.data(), width, height, AV_PIX_FMT_YUV420P, 32);
	check_quit(stat < 0, log, "Failed to allocate image");

	encoder = avcodec_find_encoder_by_name("libx264");
	check_quit(encoder == nullptr, log, "Failed to find libx264");

	encoderCtx = avcodec_alloc_context3(encoder);
	check_quit(encoderCtx == nullptr, log, "Failed to allocate codec context");

	encoderCtx->bit_rate = 15 * 1000;
	encoderCtx->width = width;
	encoderCtx->height = height;
	encoderCtx->time_base = AVRational { 60000, 1001 };
	//encoderCtx->gop_size = 10;
	encoderCtx->pix_fmt = AV_PIX_FMT_YUV420P;

	stat = av_opt_set(encoderCtx->priv_data, "preset", "superfast", 0);
	check_quit(stat < 0, log, "Failed to set veryfast");
	stat = av_opt_set(encoderCtx->priv_data, "tune", "zerolatency", 0);
	check_quit(stat < 0, log, "Failed to set zerolatency");

	stat = avcodec_open2(encoderCtx, encoder, nullptr);
	check_quit(stat < 0, log, "Failed to open codec");

	runFlag.store(true, std::memory_order_release);
	runThread = std::thread([this]() { _run(); });
}

void EncoderSoftware::stop() {
	runFlag.store(false, std::memory_order_release);
	runThread.join();
}

void EncoderSoftware::_run() {
	int stat;
	AVPacket* pkt = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();

	auto timeBegin = std::chrono::steady_clock::now();

	long long cnt = 0;
	std::shared_ptr<ExtraData> prev;
	std::deque<std::shared_ptr<ExtraData>> extraData;
	bool lastCursorVisible = false;
	int lastCursorX, lastCursorY;

	while (true) {
		if (!runFlag.load(std::memory_order_acquire))
			avcodec_send_frame(encoderCtx, nullptr);

		stat = avcodec_receive_packet(encoderCtx, pkt);
		if (stat >= 0) {
			int idx = -1;
			for (int i = 0; i < extraData.size(); i++) {
				if (extraData[i]->sampleTime == pkt->pts) {
					idx = i;
					break;
				}
			}
			check_quit(idx == -1, log, "Failed to find matching extra data for pts");

			std::shared_ptr<ExtraData> now = std::move(extraData[idx]);
			if (idx == 0)
				extraData.pop_front();
			else
				extraData.erase(extraData.begin() + idx);

			EncoderData enc;
			enc.desktopImage.insert(enc.desktopImage.end(), pkt->buf->data, pkt->buf->data + pkt->buf->size);
			enc.cursorVisible = now->cursorVisible;
			enc.cursorX = now->cursorX;
			enc.cursorY = now->cursorY;
			enc.cursorShapeUpdated = now->cursorShapeUpdated;
			if (enc.cursorShapeUpdated) {
				enc.cursorImage = std::move(now->cursorImage);
				enc.cursorW = now->cursorW;
				enc.cursorH = now->cursorH;
				enc.hotspotX = now->hotspotX;
				enc.hotspotY = now->hotspotY;
			}

			onDataAvailable(&enc);
		}
		else if (stat == AVERROR(EAGAIN)) {
			av_frame_unref(frame);

			while ((std::chrono::steady_clock::now() - timeBegin).count() < cnt * 100000 * 1001 / 60000 * 10000)
				Sleep(0);

			std::shared_ptr<ExtraData> now = std::make_shared<ExtraData>(_fetchFrame(frame));
			extraData.emplace_back(now);
			frame->sample_aspect_ratio = { 1, 1 };
			frame->height = height;
			frame->width = width;
			frame->pts = cnt++;

			if (!now->cursorUpdated) {
				now->cursorVisible = prev->cursorVisible;
				now->cursorX = prev->cursorX;
				now->cursorY = prev->cursorY;
			}
			now->sampleTime = frame->pts;
			prev = std::move(now);

			stat = avcodec_send_frame(encoderCtx, frame);
			if (stat == AVERROR_EOF)
				continue;
			check_quit(stat < 0, log, "Unknown error from avcodec_send_Frame ({})", stat);
		}
		else if (stat == AVERROR_EOF)
			break;
		else
			error_quit(log, "Unknown error from avcodec_receive_packet ({})", stat);
	}

	av_packet_free(&pkt);
	av_frame_free(&frame);
}

ExtraData EncoderSoftware::_fetchFrame(AVFrame* frame) {
	int stat;
	CaptureDataD3D data = onFrameRequest();
	
	if (data.desktopUpdated) {
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

	frame->format = AV_PIX_FMT_YUV420P;
	frame->colorspace = AVColorSpace::AVCOL_SPC_BT709;
	std::copy(frameData.begin(), frameData.end(), frame->data);
	std::copy(frameLinesize.begin(), frameLinesize.end(), frame->linesize);

	ExtraData ret;
	ret.cursorUpdated = data.cursorUpdated;
	ret.cursorVisible = data.cursorVisible;
	ret.cursorX = data.cursorX;
	ret.cursorY = data.cursorY;
	ret.cursorShapeUpdated = data.cursorShapeUpdated;
	ret.cursorImage = std::move(data.cursorImage);
	ret.cursorW = data.cursorW;
	ret.cursorH = data.cursorH;
	ret.hotspotX = data.hotspotX;
	ret.hotspotY = data.hotspotY;
	return ret;
}
