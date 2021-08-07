#include "EncoderSoftware.h"

#include <algorithm>
#include <deque>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;


EncoderSoftware::EncoderSoftware(int _width, int _height) :
	log(createNamedLogger("EncoderSoftware")),
	width(_width), height(_height)
{
}

EncoderSoftware::~EncoderSoftware() {
}

void EncoderSoftware::start() {
	int stat;

	encoder = avcodec_find_encoder_by_name("libx264");
	check_quit(encoder == nullptr, log, "Failed to find libx264");

	encoderCtx = avcodec_alloc_context3(encoder);
	check_quit(encoderCtx == nullptr, log, "Failed to allocate codec context");

	encoderCtx->bit_rate = 8 * 1000 * 1000;
	encoderCtx->width = width;
	encoderCtx->height = height;
	encoderCtx->time_base = AVRational { 60000, 1001 };
	encoderCtx->gop_size = 60;
	encoderCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	encoderCtx->thread_count = 0;
	encoderCtx->thread_type = FF_THREAD_SLICE;

	AVDictionary* opts = nullptr;
	av_dict_set(&opts, "preset", "ultrafast", 0);
	av_dict_set(&opts, "tune", "zerolatency", 0);

	stat = avcodec_open2(encoderCtx, encoder, &opts);
	check_quit(stat < 0, log, "Failed to open codec");

	runFlag.store(true, std::memory_order_release);
	runThread = std::thread([this]() { _run(); });
}

void EncoderSoftware::stop() {
	runFlag.store(false, std::memory_order_release);
	runThread.join();

	avcodec_free_context(&encoderCtx);
}

void EncoderSoftware::_run() {
	int stat;
	AVPacket* pkt = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();

	auto timeBegin = std::chrono::steady_clock::now();

	long long cnt = 0;
	CaptureData<long long> prev;
	std::deque<CaptureData<long long>> extraData;
	bool lastCursorVisible = false;
	int lastCursorX, lastCursorY;

	while (true) {
		if (!runFlag.load(std::memory_order_acquire))
			avcodec_send_frame(encoderCtx, nullptr);

		stat = avcodec_receive_packet(encoderCtx, pkt);
		if (stat >= 0) {
			int idx = -1;
			for (int i = 0; i < extraData.size(); i++) {
				if (*extraData[i].desktop == pkt->pts) {
					idx = i;
					break;
				}
			}
			check_quit(idx == -1, log, "Failed to find matching extra data for pts");

			CaptureData<long long> now = std::move(extraData[idx]);
			if (idx == 0)
				extraData.pop_front();
			else
				extraData.erase(extraData.begin() + idx);

			CaptureData<ByteBuffer> enc;
			enc.desktop = std::make_shared<ByteBuffer>(pkt->buf->size);
			enc.cursor = std::move(now.cursor);
			enc.cursorShape = std::move(now.cursorShape);

			enc.desktop->write(0, pkt->buf->data, pkt->buf->size);

			onDataAvailable(std::move(enc));
		}
		else if (stat == AVERROR(EAGAIN)) {
			av_frame_unref(frame);

			CaptureData<TextureSoftware> cap = onFrameRequest();
			check_quit(cap.desktop == nullptr, log, "Texture should've been duplicated by now");

			// Data gets freed when cap is deleted.
			// This is safe because avcodec_send_frame is suppossed to copy data.
			// May be improved to use reference counted buffer
			std::copy(cap.desktop->data, cap.desktop->data + 4, frame->data);
			std::copy(cap.desktop->linesize, cap.desktop->linesize + 4, frame->linesize);
			frame->format = AV_PIX_FMT_YUV420P;
			frame->sample_aspect_ratio = { 1, 1 };
			frame->height = height;
			frame->width = width;
			frame->pts = cnt++;

			CaptureData<long long> now;
			now.desktop = std::make_shared<long long>(frame->pts);
			now.cursor = std::move(cap.cursor);
			now.cursorShape = std::move(cap.cursorShape);
			extraData.push_back(now);
			if (now.cursor == nullptr)
				now.cursor = prev.cursor;

			prev = std::move(now);

			stat = avcodec_send_frame(encoderCtx, frame);
			if (stat == AVERROR_EOF)
				continue;
			check_quit(stat < 0, log, "Unknown error from avcodec_send_frame ({})", stat);
		}
		else if (stat == AVERROR_EOF)
			break;
		else
			error_quit(log, "Unknown error from avcodec_receive_packet ({})", stat);
	}

	av_packet_free(&pkt);
	av_frame_free(&frame);
}
