#ifndef CLIENT_HUB_WINDOW_H_
#define CLIENT_HUB_WINDOW_H_


#include "common/log.h"

#include <QtWidgets/qwidget.h>
#include <QtWidgets/qpushbutton.h>
#include <QtWidgets/qboxlayout.h>
#include <QtWidgets/qlineedit.h>
#include <QtCore/qpointer.h>

#include <atomic>


class HubWindow : public QWidget {
	Q_OBJECT;

public:
	HubWindow();
	virtual ~HubWindow();

	void showCentered();

	QSize sizeHint() const override;

private:
	LoggerPtr log;

	QHBoxLayout* rootBox;
	QVBoxLayout* connBox;
	QHBoxLayout* addrBox;
	QLineEdit* addrEdit;
	QLineEdit* portEdit;
	QPushButton* connBtn;
	QPointer<QWidget> streamWindow;

	void onClickConnect(bool checked);
};


#endif