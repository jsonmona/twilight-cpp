#ifndef CLIENT_STREAM_WINDOW_H_
#define CLIENT_STREAM_WINDOW_H_

#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qwidget.h>
#include <packet.pb.h>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "client/HostList.h"
#include "client/StreamClient.h"
#include "client/StreamViewerBase.h"
#include "common/ByteBuffer.h"
#include "common/log.h"
#include "common/util.h"

class StreamClient;

class StreamWindow : public QWidget {
    Q_OBJECT;

public:
    explicit StreamWindow(HostListEntry host);
    ~StreamWindow();

signals:
    void showLater();
    void closeLater();
    void displayPinLater(int pin);

private slots:
    void displayPin_(int pin);

private:
    LoggerPtr log;
    StreamClient sc;

    QVBoxLayout boxLayout;
    StreamViewerBase *viewer;

    std::atomic<bool> flagRunAudio;
    std::atomic<bool> flagPinBoxClosed;

    std::mutex pinBoxLock;
    std::condition_variable pinBoxClosedCV;

    std::thread audioThread;
    std::mutex audioDataLock;
    std::condition_variable audioDataCV;
    std::deque<ByteBuffer> audioData;

    void processStateChange_(StreamClient::State newState, std::string_view msg);
    void processNewPacket_(const msg::Packet &pkt, uint8_t *extraData);

    void runAudio_();
};

#endif