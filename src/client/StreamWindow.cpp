#include "StreamWindow.h"

#include "common/RingBuffer.h"
#include "common/platform/windows/winheaders.h"

#include "client/platform/windows/StreamViewerD3D.h"

#include <opus.h>
#include <cubeb/cubeb.h>

#include <QtGui/qevent.h>
#include <QtWidgets/qmessagebox.h>


StreamWindow::StreamWindow(HostListEntry host) : QWidget(),
	log(createNamedLogger("StreamWindow")),
	viewer(new StreamViewerD3D()),
	boxLayout(this)
{
	connect(this, &StreamWindow::showLater, this, &StreamWindow::show);
	connect(this, &StreamWindow::closeLater, this, &StreamWindow::close);
	connect(this, &StreamWindow::displayPinLater, this, &StreamWindow::displayPin_);
	setAttribute(Qt::WA_DeleteOnClose);

	sc.setOnNextPacket([this](const msg::Packet& pkt, uint8_t* extraData) { processNewPacket_(pkt, extraData); });
	sc.setOnStateChange([this](StreamClient::State newState, std::string_view msg) { processStateChange_(newState, msg); });
	sc.setOnDisplayPin([this](int pin) {
		std::unique_lock lock(pinBoxLock);
		flagPinBoxClosed.store(false, std::memory_order_relaxed);
		displayPinLater(pin);
		while (!flagPinBoxClosed.load(std::memory_order_relaxed))
			pinBoxClosedCV.wait(lock);
	});
	sc.connect(host);

	boxLayout.addWidget(viewer);
	boxLayout.setContentsMargins(0, 0, 0, 0);

	flagRunAudio.store(true, std::memory_order_release);
	audioThread = std::thread(&StreamWindow::runAudio_, this);
}

StreamWindow::~StreamWindow() {
	sc.disconnect();

	flagRunAudio.store(false, std::memory_order_release);
	audioDataCV.notify_all();
	audioThread.join();
}

void StreamWindow::displayPin_(int pin) {
	//TODO: Make message box look better
	QString pinText = QStringLiteral("%1").arg(pin / 10000, 4, 10, QLatin1Char('0'));
	pinText.append(' ');
	pinText.append(QStringLiteral("%1").arg(pin % 10000, 4, 10, QLatin1Char('0')));

	//FIXME: Local event loop
	QMessageBox msg;
	msg.setWindowTitle("Authentication required");
	msg.setText(QStringLiteral("Enter following pin in the server: ") + pinText);
	msg.setIcon(QMessageBox::Icon::Information);
	msg.setStandardButtons(QMessageBox::StandardButton::Ok);
	msg.exec();

	std::lock_guard lock(pinBoxLock);
	flagPinBoxClosed.store(true, std::memory_order_relaxed);
	pinBoxClosedCV.notify_all();
}

void StreamWindow::processStateChange_(StreamClient::State newState, std::string_view msg) {
	switch (newState) {
	case StreamClient::State::CONNECTED:
		log->debug("State changed to CONNECTED; {}", msg);
		showLater();
		break;
	case StreamClient::State::DISCONNECTED:
		log->debug("State changed to DISCONNECTED; {}", msg);
		closeLater();  //TODO: Add a dialog showing reason (unless it was user operation)
		break;
	}
}

void StreamWindow::processNewPacket_(const msg::Packet& pkt, uint8_t* extraData) {
	switch (pkt.msg_case()) {
	case msg::Packet::kDesktopFrame:
		viewer->processDesktopFrame(pkt, extraData);
		break;
	case msg::Packet::kCursorShape:
		viewer->processCursorShape(pkt, extraData);
		break;
	case msg::Packet::kAudioFrame: {
		ByteBuffer buf(pkt.extra_data_len());
		buf.write(0, extraData, pkt.extra_data_len());

		std::lock_guard lock(audioDataLock);
		audioData.push_back(std::move(buf));
		audioDataCV.notify_one();
		break;
	}
	case msg::Packet::kServerPerfReport: {
		pkt.server_perf_report();
		break;
	}
	default:
		log->warn("Unknown packet type: {}", pkt.msg_case());
	}
}

struct CubebUserData {
	LoggerPtr log;
	std::mutex dataLock;
	RingBuffer<float, 5760 * 4> data;
	std::atomic<bool> flagBufferUnderrun;
};

static long data_cb(cubeb_stream* stm, void* user, const void* input_buffer, void* output_buffer, long nframes) {
	CubebUserData* self = reinterpret_cast<CubebUserData*>(user);

	float* p = reinterpret_cast<float*>(output_buffer);

	int requestedLength = nframes * 2;
	int readAmount;
	/* lock */ {
		std::lock_guard lock(self->dataLock);
		readAmount = std::min<int>(requestedLength, self->data.size());
		self->data.read(reinterpret_cast<float*>(output_buffer), readAmount);
	}

	if (readAmount < requestedLength) {
		memset(p + readAmount, 0, (requestedLength - readAmount) * sizeof(float));
		self->flagBufferUnderrun.store(true, std::memory_order_relaxed);
	}

	return nframes;
}

static void state_cb(cubeb_stream* stm, void* user, cubeb_state state) {
	CubebUserData* self = reinterpret_cast<CubebUserData*>(user);
}

void StreamWindow::runAudio_() {
	HRESULT hr;
	int stat;

	hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	check_quit(FAILED(hr), log, "Failed to initialize COM in multithreaded mode");

	OpusDecoder* opusDecoder = opus_decoder_create(48000, 2, &stat);
	check_quit(stat < 0, log, "Failed to create opus decoder");

	std::vector<float> pcm(5760 * 2);

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

	while (flagRunAudio.load(std::memory_order_relaxed)) {
		ByteBuffer nowData;

		/* lock */ {
			std::unique_lock lock(audioDataLock);

			while (audioData.empty() && flagRunAudio.load(std::memory_order_acquire))
				audioDataCV.wait(lock);

			if (!flagRunAudio.load(std::memory_order_relaxed))
				break;

			nowData = std::move(audioData.front());
			audioData.pop_front();
		}

		stat = opus_decode_float(opusDecoder, nowData.data(), nowData.size(), pcm.data(), 5760, 0);
		check_quit(stat < 0, log, "Failed to decode opus stream");
		int decodedFrames = stat;

		std::lock_guard lock(self->dataLock);
		if (self->data.available() < decodedFrames * 2)
			self->data.drop(decodedFrames * 2 - self->data.available());
		self->data.write(pcm.data(), decodedFrames * 2);
	}

	stat = cubeb_stream_stop(stm);
	check_quit(stat != CUBEB_OK, log, "Failed to stop cubeb stream");

	cubeb_stream_destroy(stm);
	cubeb_destroy(cubebCtx);
	cubebCtx = nullptr;

	if (opusDecoder != nullptr) {
		opus_decoder_destroy(opusDecoder);
		opusDecoder = nullptr;
	}

	CoUninitialize();
}