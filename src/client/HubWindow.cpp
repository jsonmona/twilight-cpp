#include "HubWindow.h"

#include "client/StreamWindow.h"

#include "QtWidgets/qapplication.h"
#include "QtGui/qscreen.h"
#include <QtGui/qevent.h>


HubWindow::HubWindow() : QWidget(),
	log(createNamedLogger("HubWindow"))
{
	addrEdit = new QLineEdit();
	addrEdit->setPlaceholderText("127.0.0.1");
	portEdit = new QLineEdit();
	portEdit->setPlaceholderText("6946");
	addrBox = new QHBoxLayout();
	addrBox->addWidget(addrEdit);
	addrBox->addWidget(portEdit);

	connBtn = new QPushButton("Connect");  //TODO: Localization
	connect(connBtn, &QAbstractButton::clicked, this, &HubWindow::onClickConnect);
	connBox = new QVBoxLayout();
	connBox->addLayout(addrBox);
	connBox->addWidget(connBtn);

	rootBox = new QHBoxLayout(this);
	rootBox->addLayout(connBox);

	setAttribute(Qt::WA_DeleteOnClose);

	qApp->setQuitOnLastWindowClosed(false);
}

HubWindow::~HubWindow() {
	// Won't work
	//qApp->setQuitOnLastWindowClosed(false);

	qApp->quit();
}

void HubWindow::showCentered() {
	adjustSize();

	QRect geo = geometry();
	QPoint pt;
	pt.setX(geo.x() + geo.width() / 2);
	pt.setX(geo.y() + geo.height() / 2);

	QScreen* s = qApp->screenAt(pt);

	const QRect sr = s->availableGeometry();
	const QRect wr({}, frameSize().boundedTo(sr.size()));

	move(sr.center() - wr.center());
	show();
}

QSize HubWindow::sizeHint() const {
	return QSize{ 1280, 720 };
}

void HubWindow::onClickConnect(bool checked) {
	QString addr = addrEdit->text();
	if(addr.isEmpty())
		addr = addrEdit->placeholderText();
	addr = addr.trimmed();

	QString port = portEdit->text();
	if (port.isEmpty())
		port = portEdit->placeholderText();
	port = port.trimmed();

	// Non-standard port not supported for now
	assert(port == "6946");

	streamWindow = new StreamWindow(addr.toUtf8().data());

	QRect screenSize = qApp->primaryScreen()->geometry();

	int targetWidth = screenSize.width() * 5 / 6;
	int targetHeight = screenSize.height() * 5 / 6;
	streamWindow->setFixedSize(QSize(targetWidth, targetHeight));

	connect(streamWindow.get(), &QObject::destroyed, this, &QWidget::show);
	hide();
}