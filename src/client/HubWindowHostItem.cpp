#include "HubWindowHostItem.h"

HubWindowHostItem::HubWindowHostItem() {
    ui.setupUi(this);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

HubWindowHostItem::~HubWindowHostItem() {}

QSize HubWindowHostItem::sizeHint() const {
    QSize ret = {};
    ret.setWidth(100);
    ret.setHeight(150);
    return ret;
}

void HubWindowHostItem::setEntry(HostListEntry entry_) {
    entry = std::move(entry_);
    ui.labelHostName->setText(QString(entry->nickname.c_str()));
}

void HubWindowHostItem::on_btnConnect_clicked(bool check) {
    onClickConnect(entry);
}
