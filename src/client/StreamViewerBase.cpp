#include "StreamViewerBase.h"

#include "common/platform/windows/winheaders.h"

#include <packet.pb.h>
#include <algorithm>


StreamViewerBase::StreamViewerBase() : QWidget(),
	log(createNamedLogger("StreamViewerBase")),
	dec(nullptr)
{
	setMouseTracking(true);

	connect(this, &StreamViewerBase::signalUpdateCursor, this, &StreamViewerBase::slotUpdateCursor);

	flagRunAudio.store(true, std::memory_order_release);
	audioThread = std::thread(&StreamViewerBase::audioRun_, this);
}

StreamViewerBase::~StreamViewerBase() {
}

bool StreamViewerBase::onNewPacket(const msg::Packet& pkt, uint8_t* extraData) {
	switch (pkt.msg_case()) {
	case msg::Packet::kDesktopFrame:
	case msg::Packet::kCursorShape:
		processNewPacket(pkt, extraData);
		return true;
	case msg::Packet::kAudioFrame:
		handleAudioFrame_(pkt, extraData);
		return true;
	default:
		//log->warn("Received unknown packet type {}", pkt.msg_case());
		return false;
	}
}

void StreamViewerBase::mouseMoveEvent(QMouseEvent* ev) {
}

void StreamViewerBase::slotUpdateCursor() {
	//TODO: Update cursor shape
}

void StreamViewerBase::handleAudioFrame_(const msg::Packet& pkt, uint8_t* extraData) {
	int stat;
	const auto& audioFrame = pkt.audio_frame();

	if (dec == nullptr) {
		dec = opus_decoder_create(48000, audioFrame.channels(), &stat);
		check_quit(stat < 0, log, "Failed to create opus decoder");
	}

	ByteBuffer now(pkt.extra_data_len());
	now.write(0, extraData, pkt.extra_data_len());

	std::lock_guard lock(audioFrameLock);
	audioFrames.push_back(std::move(now));
}

struct CubebUserData {
	LoggerPtr log;
	std::mutex dataLock;
	std::deque<float> data;
};

constexpr int DATA_LEN = 4 * 5760 * 2;

static long data_cb(cubeb_stream* stm, void* user, const void* input_buffer, void* output_buffer, long nframes) {
	CubebUserData* self = reinterpret_cast<CubebUserData*>(user);

	float* p = reinterpret_cast<float*>(output_buffer);

	int idx = 0;
	/* lock */ {
		std::lock_guard lock(self->dataLock);
		auto itr = self->data.begin();
		auto end = self->data.end();
		while (itr != end) {
			p[idx++] = *itr;
			++itr;
			if (idx == nframes * 2)
				break;
		}
		self->data.erase(self->data.begin(), itr);
	}

	if(idx < nframes * 2)
		memset(p + idx, 0, (nframes * 2 - idx) * sizeof(float));

	return nframes;
}

static void state_cb(cubeb_stream* stm, void* user, cubeb_state state) {
	CubebUserData* self = reinterpret_cast<CubebUserData*>(user);
}

void StreamViewerBase::audioRun_() {
	HRESULT hr;
	int stat;

	hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	check_quit(FAILED(hr), log, "Failed to initialize COM in multithreaded mode");

	std::unique_ptr<CubebUserData> self = std::make_unique<CubebUserData>();
	self->log = log;

	cubeb* cubebCtx = nullptr;
	stat = cubeb_init(&cubebCtx, "Daylight client", nullptr);
	check_quit(stat != CUBEB_OK, log, "Failed to intialize cubeb");

	cubeb_stream_params outParam = {};
	outParam.format = CUBEB_SAMPLE_FLOAT32NE;
	outParam.rate = 48000;
	outParam.channels = 2;
	outParam.layout = CUBEB_LAYOUT_STEREO;
	outParam.prefs = CUBEB_STREAM_PREF_NONE;

	uint32_t latencyFrames;
	stat = cubeb_get_min_latency(cubebCtx, &outParam, &latencyFrames);
	check_quit(stat != CUBEB_OK, log, "Failed to get minimum latency of cubeb");

	cubeb_stream* stm;
	stat = cubeb_stream_init(cubebCtx, &stm, "Remote desktop speaker",
		nullptr, nullptr,
		nullptr, &outParam,
		latencyFrames,
		data_cb, state_cb,
		reinterpret_cast<void*>(self.get()));
	check_quit(stat != CUBEB_OK, log, "Failed to init a cubeb stream");

	stat = cubeb_stream_start(stm);
	check_quit(stat != CUBEB_OK, log, "Failed to start cubeb stream");

	while (flagRunAudio.load(std::memory_order_acquire)) {
		bool hasNewFrame = false;
		ByteBuffer nowFrame;

		/* lock */ {
			std::lock_guard lock(audioFrameLock);
			if (!audioFrames.empty()) {
				hasNewFrame = true;
				nowFrame = std::move(audioFrames.front());
				audioFrames.pop_front();
			}
		}

		if (hasNewFrame) {
			float pcm[5760 * 2];
			stat = opus_decode_float(dec, nowFrame.data(), nowFrame.size(), pcm, 5760, 0);
			check_quit(stat < 0, log, "Failed to decode opus stream");
			int decodedFrames = stat;

			std::lock_guard lock(self->dataLock);
			self->data.insert(self->data.end(), pcm, pcm + (decodedFrames * 2));
		}
	}

	stat = cubeb_stream_stop(stm);
	check_quit(stat != CUBEB_OK, log, "Failed to stop cubeb stream");

	cubeb_stream_destroy(stm);
	cubeb_destroy(cubebCtx);

	CoUninitialize();
}
