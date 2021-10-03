#ifndef CLIENT_HUB_WINDOW_HOST_ITEM_H_
#define CLIENT_HUB_WINDOW_HOST_ITEM_H_


#include "ui_HubWindowHostItem.h"

#include "client/HostList.h"

#include <QtWidgets/qframe.h>


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