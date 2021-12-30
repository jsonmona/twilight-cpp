#ifndef TWILIGHT_CLIENT_HUBWINDOWHOSTITEM_H
#define TWILIGHT_CLIENT_HUBWINDOWHOSTITEM_H

#include <QtWidgets/qframe.h>

#include "client/HostList.h"
#include "ui_HubWindowHostItem.h"

class HubWindowHostItem : public QFrame {
    Q_OBJECT;

public:
    HubWindowHostItem();
    virtual ~HubWindowHostItem();

    QSize sizeHint() const override;

    void setEntry(HostListEntry entry);

signals:
    void onClickConnect(HostListEntry entry);

private slots:
    void on_btnConnect_clicked(bool check);

private:
    Ui::HubWindowHostItem ui;
    HostListEntry entry;
};

#endif