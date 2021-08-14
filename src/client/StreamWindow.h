#ifndef CLIENT_STREAM_WINDOW_H_
#define CLIENT_STREAM_WINDOW_H_


#include "common/log.h"
#include "common/ByteBuffer.h"
#include "common/util.h"

#include "client/StreamViewerBase.h"
#include "client/StreamClient.h"

#include <QtWidgets/qwidget.h>
#include <QtWidgets/qboxlayout.h>

#include <packet.pb.h>

#include <memory>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>


class StreamClient;


class StreamWindow : public QWidget {
	Q_OBJECT;

public:
	explicit StreamWindow(const char* addr);
	~StreamWindow();

private:
	LoggerPtr log;
	StreamClient sc;

	QVBoxLayout boxLayout;
	StreamViewerBase* viewer;

	std::atomic<bool> flagRunAudio;

	std::thread audioThread;
	std::mutex audioDataLock;
	std::condition_variable audioDataCV;
	std::deque<ByteBuffer> audioData;

	void processNewPacket_(const msg::Packet& pkt, uint8_t* extraData);

	void runAudio_();
};


#endif