#include "StreamServer.h"

#include <mbedtls/net.h>

static const char PORT[] = "6495";


StreamServer::StreamServer() :
	log(createNamedLogger("StreamServer"))
{
	mbedtls_net_init(&serverSock);
	mbedtls_net_init(&sock);
}

StreamServer::~StreamServer() {
	mbedtls_net_free(&serverSock);
	mbedtls_net_free(&sock);
}

void StreamServer::start() {
	//TODO: Allow IPv6 too, using separate server socket
	int ret = mbedtls_net_bind(&serverSock, "0.0.0.0", PORT, MBEDTLS_NET_PROTO_TCP);
	check_quit(ret < 0, log, "Failed to bind server socket");

	flagRun.store(true, std::memory_order_release);
	listenThread = std::thread([this]() { _runListen(); });
}

void StreamServer::stop() {
	flagRun.store(false, std::memory_order_release);
	listenThread.join();

	output.reset();
	mbedtls_net_close(&sock);
}

void StreamServer::_runListen() {
	int ret;

	while (flagRun.load(std::memory_order_acquire)) {
		ret = mbedtls_net_accept(&serverSock, &sock, nullptr, 0, nullptr);
		check_quit(ret < 0, log, "Failed to accept socket: {}", ret);
		log->info("Accepted connection");
		output.init(&sock);

		if (capture == nullptr) {
			capture = CapturePipeline::createInstance();
			capture->setOutputCallback([this](CaptureData<std::vector<uint8_t>>&& cap) { _processOutput(std::move(cap)); });
		}
		capture->start();

		//FIXME: Wait for connection to close (or drop)
		while (flagRun.load(std::memory_order_acquire))
			mbedtls_net_usleep(10000);
	}

	capture->stop();
}

void StreamServer::_processOutput(CaptureData<std::vector<uint8_t>>&& cap) {
	if (cap.cursor)
		cursorData = std::move(cap.cursor);

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

	if(cursorData && cap.desktop) {
		msg::DesktopFrame* m = pkt.mutable_desktop_frame();
		m->set_cursor_visible(cursorData->visible);
		if (cursorData->visible) {
			m->set_cursor_x(cursorData->posX);
			m->set_cursor_y(cursorData->posY);
		}

		pkt.set_extra_data_len(cap.desktop->size());
		_writeOutput(pkt, cap.desktop->data());
	}
}

void StreamServer::_writeOutput(const msg::Packet& pck, const uint8_t* extraData) {
	uint32_t extraDataLen = pck.extra_data_len();

	auto coded = output.coded();

	check_quit(pck.ByteSizeLong() > UINT32_MAX, log, "Packet too large");

	coded->WriteVarint32(static_cast<uint32_t>(pck.ByteSizeLong()));
	pck.SerializeToCodedStream(coded.get());
	
	if (extraDataLen > 0)
		coded->WriteRaw(extraData, extraDataLen);
}