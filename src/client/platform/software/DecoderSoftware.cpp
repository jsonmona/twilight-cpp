#include "DecoderSoftware.h"


struct DecoderSoftware::EncodedData {
	uint8_t* data;
	size_t len;
};


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

	while (flagRun.load(std::memory_order_acquire)) {
		std::unique_ptr<EncodedData> data;

		while (true) {
			stat = avcodec_receive_frame(codecCtx, frame);
			if (stat == 0) {
				TextureSoftware yuv = TextureSoftware::reference(frame->data, frame->linesize,
						frame->width, frame->height, static_cast<AVPixelFormat>(frame->format));

				scale.pushInput(std::move(yuv));
				onFrameAvailable(scale.popOutput());
			}
			else if (stat == AVERROR(EAGAIN))
				break;
			else if (stat == AVERROR_EOF)
				break;
			else
				error_quit(log, "Unknown error from avcodec_receive_frame: {}", stat);
		}

		/* lock_guard */ {
			std::unique_lock<std::mutex> lock(decoderQueueMutex);

			while (decoderQueue.empty() && flagRun.load(std::memory_order_relaxed))
				decoderQueueCV.wait(lock);

			data = std::move(decoderQueue.front());
			decoderQueue.pop_front();
		}

		if (!flagRun.load(std::memory_order_relaxed))
			break;

		av_packet_unref(pkt);
		pkt->buf = nullptr;
		pkt->data = data->data;
		pkt->size = data->len;
		pkt->dts = AV_NOPTS_VALUE;
		pkt->pts = AV_NOPTS_VALUE;
		//pkt->flags = AV_PKT_FLAG_KEY;
		pkt->flags = 0;

		stat = avcodec_send_packet(codecCtx, pkt);

		if (stat == AVERROR(EAGAIN)) {
			std::lock_guard<std::mutex> lock(decoderQueueMutex);
			decoderQueue.emplace_front(std::move(data));
		}
		else
			av_free(data->data);

		if (stat == AVERROR_EOF)
			break;
		else if (stat == AVERROR(EAGAIN)) {}
		else if (stat < 0)
			error_quit(log, "Unknown error from avcoded_send_packet: {}", stat);
	}

	av_packet_free(&pkt);
	av_frame_free(&frame);
}

void DecoderSoftware::pushData(uint8_t* data, size_t len) {
	constexpr static int PAD = 64;

	uint8_t* paddedData = reinterpret_cast<uint8_t*>(av_malloc(len + PAD));
	memcpy(paddedData, data, len);
	memset(paddedData + len, 0, PAD);

	std::lock_guard<std::mutex> lock(decoderQueueMutex);
	decoderQueue.emplace_back(std::make_unique<EncodedData>(EncodedData{ paddedData, len }));
	decoderQueueCV.notify_one();
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
	decoderQueueCV.notify_all();
	looper.join();

	avcodec_free_context(&codecCtx);
}
