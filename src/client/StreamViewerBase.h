#ifndef CLIENT_STREAM_VIEWER_H_
#define CLIENT_STREAM_VIEWER_H_


#include <packet.pb.h>

#include <QtWidgets/qwidget.h>


class StreamViewerBase : public QWidget {
	Q_OBJECT;

public:
	StreamViewerBase() {}
	~StreamViewerBase() override {}

	virtual void setDrawCursor(bool newval) = 0;

	virtual void processDesktopFrame(const msg::Packet& pkt, uint8_t* extraData) = 0;
	virtual void processCursorShape(const msg::Packet& pkt, uint8_t* extraData) = 0;
};


#endif