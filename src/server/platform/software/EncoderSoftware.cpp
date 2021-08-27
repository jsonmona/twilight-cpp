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
	av_dict_set(&opts, "x264-params", "keyint=infinite", 0);

	stat = avcodec_open2(encoderCtx, encoder, &opts);
	check_quit(stat < 0, log, "Failed to open codec");

	if (av_dict_count(opts) > 0) {
		std::string buf;
		AVDictionaryEntry* entry = av_dict_get(opts, "", nullptr, AV_DICT_IGNORE_SUFFIX);
		while (entry != nullptr) {
			buf += entry->key;
			buf += "=";
			buf += entry->value;
			buf += " ";
			entry = av_dict_get(opts, "", entry, AV_DICT_IGNORE_SUFFIX);
		}
		log->warn("libx264 ignored some options: {}", buf);
	}
	av_dict_free(&opts);

	flagRun.store(true, std::memory_order_release);
	runThread = std::thread([this]() { _run(); });
}

void EncoderSoftware::stop() {
	flagRun.store(false, std::memory_order_release);
	runThread.join();

	avcodec_free_context(&encoderCtx);
}

void EncoderSoftware::_run() {
	int stat;
	AVPacket* pkt = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();

	auto timeBegin = std::chrono::steady_clock::now();

	struct SideData {
		long long pts;
		std::chrono::steady_clock::time_point startTime;
	};

	long long cnt = 0;
	DesktopFrame<SideData> prev;
	std::deque<DesktopFrame<SideData>> extraData;
	bool lastCursorVisible = false;
	int lastCursorX, lastCursorY;

	while (true) {
		if (!flagRun.load(std::memory_order_acquire))
			avcodec_send_frame(encoderCtx, nullptr);

		stat = avcodec_receive_packet(encoderCtx, pkt);
		if (stat >= 0) {
			int idx = -1;
			for (int i = 0; i < extraData.size(); i++) {
				if (extraData[i].desktop->pts == pkt->pts) {
					idx = i;
					break;
				}
			}
			check_quit(idx == -1, log, "Failed to find matching extra data for pts");

			DesktopFrame<SideData> now = std::move(extraData[idx]);
			if (idx == 0)
				extraData.pop_front();
			else
				extraData.erase(extraData.begin() + idx);

			DesktopFrame<ByteBuffer> enc;
			enc.desktop = std::make_shared<ByteBuffer>(pkt->buf->size);
			enc.cursorPos = std::move(now.cursorPos);
			enc.cursorShape = std::move(now.cursorShape);

			enc.desktop->write(0, pkt->buf->data, pkt->buf->size);

			auto timeDiff = std::chrono::steady_clock::now() - now.desktop->startTime;
			statMixer.pushValue(std::chrono::duration_cast<std::chrono::duration<float>>(timeDiff).count());

			onDataAvailable(std::move(enc));
		}
		else if (stat == AVERROR(EAGAIN)) {
			av_frame_unref(frame);

			DesktopFrame<TextureSoftware> cap;

			/* lock */ {
				std::unique_lock lock(dataLock);
				while (dataQueue.empty() && flagRun.load(std::memory_order_relaxed))
					dataCV.wait(lock);

				if (!flagRun.load(std::memory_order_relaxed))
					continue;

				if (dataQueue.size() > 50)
					log->warn("High number of pending textures for encoder ({}). Is encoder overloaded?", dataQueue.size());

				cap = std::move(dataQueue.front());
				dataQueue.pop_front();
			}

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

			DesktopFrame<SideData> now;
			now.desktop = std::make_shared<SideData>(SideData{ frame->pts, std::chrono::steady_clock::now() });
			now.cursorPos = std::move(cap.cursorPos);
			now.cursorShape = std::move(cap.cursorShape);
			extraData.push_back(now);
			if (now.cursorPos == nullptr)
				now.cursorPos = prev.cursorPos;

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

void EncoderSoftware::pushData(DesktopFrame<TextureSoftware>&& newData) {
	std::lock_guard lock(dataLock);
	dataQueue.push_back(std::move(newData));
	dataCV.notify_one();
}
