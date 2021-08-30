#include "DecoderSoftware.h"

#include <map>


DecoderSoftware::DecoderSoftware() :
	log(createNamedLogger("DecoderSoftware")),
	codec(nullptr), codecCtx(nullptr),
	flagRun(false)
{
}

DecoderSoftware::~DecoderSoftware() {
	check_quit(flagRun, log, "Being destructed without stopping");
	avcodec_free_context(&codecCtx);
}

void DecoderSoftware::_run() {
	int stat;
	AVPacket* pkt = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();

	long long currPts = 0;
	std::map<long long, DesktopFrame<std::chrono::steady_clock::time_point>> sidedata;

	while (flagRun.load(std::memory_order_acquire)) {
		while (true) {
			stat = avcodec_receive_frame(codecCtx, frame);
			if (stat == 0) {
				DesktopFrame<TextureSoftware> result;

				TextureSoftware yuv = TextureSoftware::reference(frame->data, frame->linesize,
						frame->width, frame->height, static_cast<AVPixelFormat>(frame->format));

				scale.pushInput(std::move(yuv));
				result.desktop = std::make_shared<TextureSoftware>(scale.popOutput());

				auto itr = sidedata.find(frame->pts);
				if (itr != sidedata.end()) {
					result.cursorPos = std::move(itr->second.cursorPos);
					result.cursorShape = std::move(itr->second.cursorShape);
					sidedata.erase(itr);
				}
				else {
					log->error("Unable to find sidedata for pts={}  (sidedata.size={})", frame->pts, sidedata.size());
				}

				std::lock_guard lock(frameLock);
				frameQueue.push_back(std::move(result));
				frameCV.notify_one();
			}
			else if (stat == AVERROR(EAGAIN))
				break;
			else if (stat == AVERROR_EOF)
				break;
			else
				error_quit(log, "Unknown error from avcodec_receive_frame: {}", stat);
		}

		DesktopFrame<std::pair<uint8_t*, size_t>> data;

		/* lock_guard */ {
			std::unique_lock lock(packetLock);

			while (packetQueue.empty() && flagRun.load(std::memory_order_relaxed))
				packetCV.wait(lock);

			if (!flagRun.load(std::memory_order_relaxed))
				break;

			data = std::move(packetQueue.front());
			packetQueue.pop_front();
		}

		if (!flagRun.load(std::memory_order_relaxed))
			break;

		av_packet_unref(pkt);
		pkt->buf = nullptr;
		pkt->data = data.desktop->first;
		pkt->size = data.desktop->second;
		pkt->dts = AV_NOPTS_VALUE;
		pkt->pts = currPts;
		//pkt->flags = AV_PKT_FLAG_KEY;
		pkt->flags = 0;

		stat = avcodec_send_packet(codecCtx, pkt);

		if (stat == AVERROR(EAGAIN)) {
			std::lock_guard lock(packetLock);
			packetQueue.push_front(std::move(data));
		}
		else {
			av_free(data.desktop->first);

			DesktopFrame<std::chrono::steady_clock::time_point> now;
			now.desktop = std::make_shared<std::chrono::steady_clock::time_point>(std::chrono::steady_clock::now());
			now.cursorPos = std::move(data.cursorPos);
			now.cursorShape = std::move(data.cursorShape);
			sidedata[currPts++] = std::move(now);
		}

		if (stat == AVERROR_EOF)
			break;
		else if (stat == AVERROR(EAGAIN)) {}
		else if (stat < 0)
			error_quit(log, "Unknown error from avcoded_send_packet: {}", stat);
	}

	av_packet_free(&pkt);
	av_frame_free(&frame);
}

void DecoderSoftware::pushData(DesktopFrame<ByteBuffer>&& nextData) {
	constexpr static int PAD = 64;

	uint8_t* paddedData = reinterpret_cast<uint8_t*>(av_malloc(nextData.desktop->size() + PAD));
	memcpy(paddedData, nextData.desktop->data(), nextData.desktop->size());
	memset(paddedData + nextData.desktop->size(), 0, PAD);

	DesktopFrame<std::pair<uint8_t*, size_t>> now;
	now.desktop = std::make_shared<std::pair<uint8_t*, size_t>>(paddedData, nextData.desktop->size());
	now.cursorPos = std::move(nextData.cursorPos);
	now.cursorShape = std::move(nextData.cursorShape);

	std::lock_guard<std::mutex> lock(packetLock);
	packetQueue.push_back(std::move(now));
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

	ret = frameQueue.front();
	frameQueue.pop_front();
	return ret;
}

void DecoderSoftware::start() {
	codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	check_quit(codec == nullptr, log, "Failed to find H264 decoder");

	codecCtx = avcodec_alloc_context3(codec);
	check_quit(codecCtx == nullptr, log, "Failed to create decoder context");

	int ret = avcodec_open2(codecCtx, codec, nullptr);
	check_quit(ret < 0, log, "Failed to open decoder context");

	//FIXME: fixed output dimension
	scale.setOutputFormat(1920, 1080, AV_PIX_FMT_RGBA);

	flagRun.store(true, std::memory_order_release);
	looper = std::thread([this]() { _run(); });
}

void DecoderSoftware::stop() {
	flagRun.store(false, std::memory_order_release);
	packetCV.notify_all();
	frameCV.notify_all();

	looper.join();

	avcodec_free_context(&codecCtx);
}
