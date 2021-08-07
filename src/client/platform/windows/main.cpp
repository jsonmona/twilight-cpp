#include "common/log.h"
#include "common/platform/windows/winheaders.h"
#include "client/StreamClient.h"

#include "client/platform/windows/StreamViewerD3D.h"

#include <packet.pb.h>

#include <QtWidgets/qapplication.h>
#include <QtGui/qscreen.h>
#include <cstdio>


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	StreamClient sc;

	QApplication app(__argc, __argv);

	QRect screenSize = app.primaryScreen()->geometry();

	int targetWidth = screenSize.width() * 5 / 6;
	int targetHeight = screenSize.height() * 5 / 6;

	StreamViewerD3D sv;
	sc.setOnNextPacket([&](const msg::Packet& pkt, uint8_t* extraData) { sv.onNewPacket(pkt, extraData); });
	sc.connect("192.168.11.129");
	sv.setFixedSize(QSize(targetWidth, targetHeight));
	sv.show();

	return app.exec();
}