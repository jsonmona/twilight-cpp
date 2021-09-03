#include "common/log.h"
#include "common/platform/windows/winheaders.h"

#include "client/StreamWindow.h"

#include <packet.pb.h>

#include <QtWidgets/qapplication.h>
#include <QtGui/qscreen.h>
#include <cstdio>


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	setupLogger();

	GOOGLE_PROTOBUF_VERIFY_VERSION;

	QApplication app(__argc, __argv);

	QRect screenSize = app.primaryScreen()->geometry();

	int targetWidth = screenSize.width() * 5 / 6;
	int targetHeight = screenSize.height() * 5 / 6;
	
	StreamWindow sw("192.168.11.129");
	sw.setFixedSize(QSize(targetWidth, targetHeight));
	sw.show();

	return app.exec();
}