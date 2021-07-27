#ifndef CLIENT_STREAM_VIEWER_H_
#define CLIENT_STREAM_VIEWER_H_


#include "common/log.h"
#include "common/ByteBuffer.h"

#include <packet.pb.h>

#include <QtWidgets/qwidget.h>

#include <atomic>
#include <mutex>


class StreamViewerBase : public QWidget {
	Q_OBJECT;
	LoggerPtr log;


protected:
	std::mutex cursorShapeLock;
	int cursorWidth, cursorHeight;
	bool hasNewCursorShape;
	ByteBuffer cursorShapeData;

	virtual void processNewPacket(const msg::Packet& pkt, uint8_t* extraData) = 0;

private slots:
	void slotUpdateCursor();

signals:
	void signalUpdateCursor();

public:
	StreamViewerBase();
	~StreamViewerBase() override;

	// return true if consumed
	bool onNewPacket(const msg::Packet& pkt, uint8_t* extraData);

	void mouseMoveEvent(QMouseEvent* ev) override;
};


#endif