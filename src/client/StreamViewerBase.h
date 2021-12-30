#ifndef TWILIGHT_CLIENT_STREAMVIEWERBASE_H
#define TWILIGHT_CLIENT_STREAMVIEWERBASE_H

#include <QtWidgets/qwidget.h>
#include <packet.pb.h>

class StreamViewerBase : public QWidget {
    Q_OBJECT;

public:
    StreamViewerBase() {}
    ~StreamViewerBase() override {}

    virtual void setDrawCursor(bool newval) = 0;

    virtual void processDesktopFrame(const msg::Packet &pkt, uint8_t *extraData) = 0;
    virtual void processCursorShape(const msg::Packet &pkt, uint8_t *extraData) = 0;
};

#endif