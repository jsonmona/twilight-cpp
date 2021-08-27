#include "StreamServer.h"

constexpr uint16_t SERVICE_PORT = 6495;


StreamServer::StreamServer() :
	log(createNamedLogger("StreamServer"))
{
	capture = CapturePipeline::createInstance();
	capture->setOutputCallback([this](DesktopFrame<ByteBuffer>&& cap) { _processOutput(std::move(cap)); });
	audioEncoder.setOnAudioData([this](const uint8_t* data, size_t len) {
		msg::Packet pkt;
		pkt.set_extra_data_len(len);
		auto audioFrame = pkt.mutable_audio_frame();
		audioFrame->set_channels(2);
		_writeOutput(pkt, data);
	});

	server.setOnNewConnection([this](std::unique_ptr<NetworkSocket>&& _newSock) {
		std::unique_ptr<NetworkSocket> newSock = std::move(_newSock);

		if (conn != nullptr && conn->isConnected()) {
			log->warn("Dropping new connection because current client is already connected");
			return;
		}

		lastStatReport = std::chrono::steady_clock::now();
		conn = std::move(newSock);
		conn->setOnDisconnected([this]() {
			audioEncoder.stop();
			capture->stop();
		});
		capture->start();
		audioEncoder.start();
	});
}

StreamServer::~StreamServer() {
}

void StreamServer::start() {
	server.startListen(SERVICE_PORT);
}

void StreamServer::stop() {
	server.stopListen();

	if (conn != nullptr)
		conn->disconnect();
}

void StreamServer::_processOutput(DesktopFrame<ByteBuffer>&& cap) {
	auto nowTime = std::chrono::steady_clock::now();

	if (cap.cursorPos)
		cursorPos = std::move(cap.cursorPos);

	msg::Packet pkt;
	if (cap.cursorShape) {
		msg::CursorShape* m = pkt.mutable_cursor_shape();
		m->set_width(cap.cursorShape->width);
		m->set_height(cap.cursorShape->height);
		m->set_hotspot_x(cap.cursorShape->hotspotX);
		m->set_hotspot_y(cap.cursorShape->hotspotY);

		pkt.set_extra_data_len(cap.cursorShape->image.size());
		_writeOutput(pkt, cap.cursorShape->image.data());
	}

	if(cursorPos && cap.desktop) {
		msg::DesktopFrame* m = pkt.mutable_desktop_frame();
		m->set_cursor_visible(cursorPos->visible);
		if (cursorPos->visible) {
			m->set_cursor_x(cursorPos->x);
			m->set_cursor_y(cursorPos->y);
		}

		pkt.set_extra_data_len(cap.desktop->size());
		_writeOutput(pkt, cap.desktop->data());
	}

	if (nowTime - lastStatReport > std::chrono::milliseconds(250)) {
		lastStatReport = nowTime;
		auto cap = capture->calcCaptureStat();
		auto enc = capture->calcEncoderStat();

		if (cap.valid() && enc.valid()) {
			msg::ServerPerfReport* m = pkt.mutable_server_perf_report();
			m->set_capture_min(cap.min);
			m->set_capture_avg(cap.avg);
			m->set_capture_max(cap.max);

			m->set_encoder_min(enc.min);
			m->set_encoder_avg(enc.avg);
			m->set_encoder_max(enc.max);

			pkt.set_extra_data_len(0);
			_writeOutput(pkt, nullptr);
		}
	}
}

void StreamServer::_writeOutput(const msg::Packet& pck, const uint8_t* extraData) {
	check_quit(pck.ByteSizeLong() > UINT32_MAX, log, "Packet too large");
	uint32_t extraDataLen = pck.extra_data_len();

	//FIXME: Ugly code

	/* lock */ {
		std::lock_guard lock(connWriteLock);

		/* coded */ {
			auto coded = conn->output().coded();

			coded->WriteVarint32(static_cast<uint32_t>(pck.ByteSizeLong()));
			pck.SerializeToCodedStream(coded.get());

			if (extraDataLen > 0)
				coded->WriteRaw(extraData, extraDataLen);
		}

		conn->output().flush();
	}
}