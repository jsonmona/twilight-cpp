#ifndef CLIENT_STREAM_VIEWER_H_
#define CLIENT_STREAM_VIEWER_H_


#include <packet.pb.h>

#include <QtWidgets/qwidget.h>


class StreamViewerBase : public QWidget {
	Q_OBJECT;

public:
	StreamViewerBase();
	~StreamViewerBase() override;

	// return true if consumed
	virtual bool processNewPacket(const msg::Packet& pkt, uint8_t* extraData) = 0;
};


#endif