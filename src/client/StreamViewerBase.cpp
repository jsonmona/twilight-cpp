#include "StreamViewerBase.h"

#include <packet.pb.h>
#include <algorithm>


StreamViewerBase::StreamViewerBase() : QWidget(),
	log(createNamedLogger("StreamViewerBase")),
	hasNewCursorShape(false), cursorWidth(0), cursorHeight(0)
{
	setMouseTracking(true);

	connect(this, &StreamViewerBase::signalUpdateCursor, this, &StreamViewerBase::slotUpdateCursor);
}

StreamViewerBase::~StreamViewerBase() {
}

bool StreamViewerBase::onNewPacket(const msg::Packet& pkt, uint8_t* extraData) {
	switch (pkt.msg_case()) {
	case msg::Packet::kDesktopFrame:
		processNewPacket(pkt, extraData);
		return true;
	case msg::Packet::kCursorShape:
		processNewPacket(pkt, extraData);
		/* cursorShapeLock */ {
			std::lock_guard lock(cursorShapeLock);

			hasNewCursorShape = true;

			const msg::CursorShape& cursor = pkt.cursor_shape();
			cursorWidth = cursor.width();
			cursorHeight = cursor.height();

			int byteLen = cursorWidth * cursorHeight * 4;

			if (pkt.extra_data_len() != byteLen) {
				log->error("Cursor shape data length mismatch! (provided={}, calculated={})",
					pkt.extra_data_len(), byteLen);
			}

			if (cursorShapeData.size() < byteLen)
				cursorShapeData.resize(byteLen);
			memcpy(cursorShapeData.data(), extraData, std::min<int>(byteLen, pkt.extra_data_len()));
		}
		signalUpdateCursor();
		return true;
	default:
		return false;
	}
}

void StreamViewerBase::mouseMoveEvent(QMouseEvent* ev) {
}

void StreamViewerBase::slotUpdateCursor() {
	//TODO: Update cursor shape
}