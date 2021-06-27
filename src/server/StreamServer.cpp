#include "StreamServer.h"


StreamServer::StreamServer() : log(createNamedLogger("StreamServer")) {
}

void StreamServer::start() {
	if (capture == nullptr) {
		capture = CapturePipeline::createInstance();
		capture->setOutputCallback([this](CaptureData<std::vector<uint8_t>>&& cap) { _processOutput(std::move(cap)); });
	}

	f = fopen("../server-stream.dump", "wb");
	check_quit(f == nullptr, log, "Failed to open file to write");

	capture->start();
}

void StreamServer::stop() {
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
	// TODO: Use zero copy stream
	static thread_local std::string buf;
	buf.clear();

	uint32_t extraDataLen = pck.extra_data_len();
	pck.SerializeToString(&buf);

	uint32_t packetSize = buf.size();
	fwrite(&packetSize, 4, 1, f);

	size_t offset = 0;
	while (offset < buf.size()) {
		size_t ret = fwrite(buf.data() + offset, 1, packetSize - offset, f);
		if (ret == 0)
			error_quit(log, "Failed to write to file");
		offset += ret;
	}
	
	if (extraDataLen > 0) {
		offset = 0;
		while (offset < extraDataLen) {
			size_t ret = fwrite(extraData + offset, 1, extraDataLen - offset, f);
			if (ret == 0)
				error_quit(log, "Failed to write to file");
			offset += ret;
		}
	}
	
	fflush(f);
}