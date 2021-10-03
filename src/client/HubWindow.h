#ifndef CLIENT_HUB_WINDOW_H_
#define CLIENT_HUB_WINDOW_H_


#include "ui_HubWindow.h"

#include "common/CertStore.h"
#include "common/log.h"

#include "client/FlowLayout.h"
#include "client/HostList.h"
#include "client/HubWindowHostItem.h"

#include <QtWidgets/qwidget.h>
#include <QtWidgets/qpushbutton.h>
#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qlineedit.h>
#include <QtCore/qpointer.h>

#include <atomic>
#include <vector>


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
	QWidget* layoutWidget;
	FlowLayout* layout;
	std::vector<HubWindowHostItem*> items;
	QPointer<QWidget> streamWindow;

	HostList hostList;
	bool hostListChanged;

	void reloadItems_();
};


#endif