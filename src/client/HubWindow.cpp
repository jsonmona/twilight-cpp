#include "HubWindow.h"

#include <QtGui/qevent.h>
#include <QtGui/qscreen.h>
#include <QtWidgets/qapplication.h>
#include <QtWidgets/qdialog.h>
#include <QtWidgets/qmessagebox.h>

#include "client/StreamWindow.h"
#include "ui_HubWindowAddHostDialog.h"

HubWindow::HubWindow() : QWidget(), log(createNamedLogger("HubWindow")) {
    ui.setupUi(this);

    layoutWidget = new QWidget();
    layout = new FlowLayout(layoutWidget, 10, 7, 7);
    ui.scrollAreaHosts->setWidget(layoutWidget);

    setAttribute(Qt::WA_DeleteOnClose);

    qApp->setQuitOnLastWindowClosed(false);

    hostList.loadFromFile("hosts.toml");
    reloadItems_();
}

HubWindow::~HubWindow() {
    // Won't work
    // qApp->setQuitOnLastWindowClosed(false);

    hostList.saveToFile("hosts.toml");

    qApp->quit();
}

void HubWindow::showCentered() {
    adjustSize();

    QRect geo = geometry();
    QPoint pt;
    pt.setX(geo.x() + geo.width() / 2);
    pt.setX(geo.y() + geo.height() / 2);

    QScreen *s = qApp->screenAt(pt);

    const QRect sr = s->availableGeometry();
    const QRect wr({}, frameSize().boundedTo(sr.size()));

    move(sr.center() - wr.center());
    show();
}

QSize HubWindow::sizeHint() const {
    return QSize{1280, 720};
}

void HubWindow::on_btnAddHost_clicked(bool checked) {
    QDialog dialog(this);
    Ui::HubWindowAddHostDialog dialogUi;

    dialogUi.setupUi(&dialog);

    int ret = dialog.exec();

    if (ret == QDialog::Accepted) {
        HostListEntry entry = std::make_shared<HostList::Entry>();
        entry->nickname = dialogUi.lineEditDisplayName->text().toStdString();
        entry->addr.push_back(dialogUi.lineEditAddr->text().toStdString());
        entry->addr.push_back(dialogUi.lineEditAddrFallback->text().toStdString());

        // TODO: Add more validity checks

        if (entry->nickname.empty())
            entry->nickname = entry->addr.front();

        if (entry->addr.front().empty())
            entry->addr.erase(entry->addr.begin());
        if (entry->addr.back().empty())
            entry->addr.erase(entry->addr.end() - 1);

        if (entry->addr.size() > 1) {
            // TODO: Remove this when fallback address is supported
            QMessageBox msg;
            msg.warning(this, "Warning", "Fallback address is not supported yet", QMessageBox::Ok,
                        QMessageBox::NoButton);
        }

        if (!entry->addr.empty()) {
            hostList.hosts.push_back(std::move(entry));
            reloadItems_();
        }
    }
}

void HubWindow::connectToEntry(HostListEntry entry) {
    // TODO: Allow use of URI (so that protocol and port can be specified)

    streamWindow = new StreamWindow(entry, true);

    QRect screenSize = qApp->primaryScreen()->geometry();

    int targetWidth = screenSize.width() * 5 / 6;
    int targetHeight = screenSize.height() * 5 / 6;
    streamWindow->setFixedSize(QSize(targetWidth, targetHeight));

    connect(streamWindow.get(), &QObject::destroyed, this, &QWidget::show);
    hide();
}

void HubWindow::reloadItems_() {
    for (HubWindowHostItem *now : items) {
        now->deleteLater();
        layout->removeWidget(now);
    }

    items.clear();
    items.reserve(hostList.hosts.size());

    for (HostListEntry &now : hostList.hosts) {
        HubWindowHostItem *item = new HubWindowHostItem();
        item->setEntry(now);
        connect(item, &HubWindowHostItem::onClickConnect, this, &HubWindow::connectToEntry);

        layout->addWidget(item);
        items.push_back(item);
    }
}
