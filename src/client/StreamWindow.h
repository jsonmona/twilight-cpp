#ifndef TWILIGHT_CLIENT_STREAMWINDOW_H
#define TWILIGHT_CLIENT_STREAMWINDOW_H

#include "common/ByteBuffer.h"
#include "common/log.h"
#include "common/util.h"

#include "client/HostList.h"
#include "client/NetworkClock.h"
#include "client/StreamClient.h"
#include "client/StreamViewerBase.h"

#include <packet.pb.h>

#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qwidget.h>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

class StreamClient;

class StreamWindow : public QWidget {
    Q_OBJECT;

public:
    StreamWindow(HostListEntry host, bool playAudio);
    ~StreamWindow();

signals:
    void showLater();
    void closeLater();
    void displayPinLater(int pin);

private slots:
    void displayPin_(int pin);

private:
    static NamedLogger log;

    NetworkClock clock;
    StreamClient sc;

    QVBoxLayout boxLayout;
    StreamViewerBase *viewer;

    std::atomic<bool> flagPlayAudio;
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
    void runPing_();
};

#endif
