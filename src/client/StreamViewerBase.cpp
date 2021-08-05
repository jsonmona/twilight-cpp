#include "StreamViewerBase.h"

#include <packet.pb.h>
#include <algorithm>


StreamViewerBase::StreamViewerBase() : QWidget(),
	log(createNamedLogger("StreamViewerBase"))
{
	setMouseTracking(true);

	connect(this, &StreamViewerBase::signalUpdateCursor, this, &StreamViewerBase::slotUpdateCursor);
}

StreamViewerBase::~StreamViewerBase() {
}

bool StreamViewerBase::onNewPacket(const msg::Packet& pkt, uint8_t* extraData) {
	switch (pkt.msg_case()) {
	case msg::Packet::kDesktopFrame:
	case msg::Packet::kCursorShape:
		processNewPacket(pkt, extraData);
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