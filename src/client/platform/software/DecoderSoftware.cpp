#include "DecoderSoftware.h"

#include <map>


DecoderSoftware::DecoderSoftware() :
	log(createNamedLogger("DecoderSoftware")),
	flagRun(false)
{
}

DecoderSoftware::~DecoderSoftware() {
	check_quit(flagRun.load(std::memory_order_relaxed), log, "Being destructed without stopping");

	if (looper.joinable())
		looper.join();
}

void DecoderSoftware::run_() {
	int err;
	ISVCDecoder* decoder;

	err = loader->CreateDecoder(&decoder);
	check_quit(err != 0 || decoder == nullptr, log, "Failed to create decoder instance");

	SDecodingParam decParam = {};
	decParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
	decParam.bParseOnly = false;

	err = decoder->Initialize(&decParam);
	check_quit(err != 0, log, "Failed to initialize decoder");

	while (flagRun.load(std::memory_order_acquire)) {
		DesktopFrame<ByteBuffer> data;

		/* lock_guard */ {
			std::unique_lock lock(packetLock);

			while (packetQueue.empty() && flagRun.load(std::memory_order_relaxed))
				packetCV.wait(lock);

			if (!flagRun.load(std::memory_order_relaxed))
				break;

			data = std::move(packetQueue.front());
			packetQueue.pop_front();
		}

		uint8_t* framebuffer[3] = {};
		SBufferInfo decBufferInfo = {};
		err = decoder->DecodeFrameNoDelay(data.desktop->data(), data.desktop->size(), framebuffer, &decBufferInfo);
		check_quit(err != 0, log, "Failed to decode frame");

		if (decBufferInfo.iBufferStatus == 1) {
			int w = decBufferInfo.UsrData.sSystemBuffer.iWidth;
			int h = decBufferInfo.UsrData.sSystemBuffer.iHeight;

			EVideoFormatType openH264Format = (EVideoFormatType) decBufferInfo.UsrData.sSystemBuffer.iFormat;
			AVPixelFormat fmt = openH264Format == videoFormatI420 ? AV_PIX_FMT_YUV420P :
				AV_PIX_FMT_NONE;
			check_quit(fmt == AV_PIX_FMT_NONE, log, "Unknown OpenH264 video format {}", decBufferInfo.UsrData.sSystemBuffer.iFormat);

			int linesize[4] = {
				decBufferInfo.UsrData.sSystemBuffer.iStride[0],
				decBufferInfo.UsrData.sSystemBuffer.iStride[1],
				decBufferInfo.UsrData.sSystemBuffer.iStride[1],
				0
			};
			if (framebuffer[0] != decBufferInfo.pDst[0] || framebuffer[1] != decBufferInfo.pDst[1] || framebuffer[2] != decBufferInfo.pDst[2]) {
				log->info("linesize: {} {} {}", linesize[0], linesize[1], linesize[2]);
				log->info("framebuffer: {} {} {}", (uintptr_t)framebuffer[0], (uintptr_t)framebuffer[1], (uintptr_t)framebuffer[2]);
				log->info("bufferinfo: {} {} {}", (uintptr_t)decBufferInfo.pDst[0], (uintptr_t)decBufferInfo.pDst[1], (uintptr_t)decBufferInfo.pDst[2]);
			}

			TextureSoftware yuv = TextureSoftware::reference(framebuffer, linesize, w, h, fmt);
			scale.pushInput(std::move(yuv));

			DesktopFrame<TextureSoftware> frame;
			frame.desktop = std::make_shared<TextureSoftware>(scale.popOutput());
			frame.cursorPos = std::move(data.cursorPos);
			frame.cursorShape = std::move(data.cursorShape);

			std::lock_guard lock(frameLock);
			frameQueue.push_back(std::move(frame));
			frameCV.notify_one();
		}
		else
			log->info("No frame provided");
	}

	err = decoder->Uninitialize();
	if (err != 0)
		log->warn("Failed to uninitialize decoder");
	loader->DestroyDecoder(decoder);
}

void DecoderSoftware::pushData(DesktopFrame<ByteBuffer>&& nextData) {
	std::lock_guard<std::mutex> lock(packetLock);
	packetQueue.push_back(std::move(nextData));
	packetCV.notify_one();
}

DesktopFrame<TextureSoftware> DecoderSoftware::popData() {
	DesktopFrame<TextureSoftware> ret;

	std::lock_guard lock(packetLock);
	if (frameQueue.empty() || !flagRun.load(std::memory_order_relaxed))
		return ret;

	// Prevent excessive buffering
	frameBufferHistory.push(frameQueue.size());
	while (frameBufferHistory.max - frameBufferHistory.min + 1 < frameQueue.size())
		frameQueue.pop_front();

	ret = std::move(frameQueue.front());
	frameQueue.pop_front();
	if (ret.desktop && ret.desktop->width < 0)
		return ret;
	return ret;
}

void DecoderSoftware::start() {
	if (loader == nullptr) {
		loader = OpenH264Loader::getInstance();
		loader->prepare();
	}

	//FIXME: fixed output dimension
	scale.setOutputFormat(1920, 1080, AV_PIX_FMT_RGBA);

	flagRun.store(true, std::memory_order_release);
	looper = std::thread([this]() { run_(); });
}

void DecoderSoftware::stop() {
	flagRun.store(false, std::memory_order_release);
	packetCV.notify_all();
	frameCV.notify_all();
}
