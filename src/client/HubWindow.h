#ifndef CLIENT_HUB_WINDOW_H_
#define CLIENT_HUB_WINDOW_H_

#include <QtCore/qpointer.h>
#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qlineedit.h>
#include <QtWidgets/qpushbutton.h>
#include <QtWidgets/qwidget.h>

#include <atomic>
#include <vector>

#include "client/FlowLayout.h"
#include "client/HostList.h"
#include "client/HubWindowHostItem.h"
#include "common/CertStore.h"
#include "common/log.h"
#include "ui_HubWindow.h"

class HubWindow : public QWidget {
    Q_OBJECT;

public:
    HubWindow();
    virtual ~HubWindow();

    void showCentered();

    QSize sizeHint() const override;

private slots:
    void connectToEntry(HostListEntry entry);
    void on_btnAddHost_clicked(bool checked);

private:
    LoggerPtr log;

    Ui::HubWindow ui;
    QWidget *layoutWidget;
    FlowLayout *layout;
    std::vector<HubWindowHostItem *> items;
    QPointer<QWidget> streamWindow;

    HostList hostList;

    void reloadItems_();
};

#endif