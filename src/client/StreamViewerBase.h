#ifndef CLIENT_STREAM_VIEWER_H_
#define CLIENT_STREAM_VIEWER_H_


#include "common/log.h"
#include "common/ByteBuffer.h"
#include "common/util.h"

#include <packet.pb.h>

#include <opus.h>
#include <cubeb/cubeb.h>

#include <QtWidgets/qwidget.h>

#include <atomic>
#include <thread>
#include <mutex>
#include <deque>


class StreamViewerBase : public QWidget {
	Q_OBJECT;
	LoggerPtr log;

	OpusDecoder* dec;
	std::thread audioThread;
	std::atomic<bool> flagRunAudio;
	std::mutex audioFrameLock;
	std::condition_variable audioFrameCV;
	std::deque<ByteBuffer> audioFrames;

private:
	void handleAudioFrame_(const msg::Packet& pkt, uint8_t* extraData);
	void audioRun_();

protected:
	virtual bool useAbsCursor() = 0;
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