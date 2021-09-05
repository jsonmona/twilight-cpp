#include "common/log.h"
#include "common/platform/windows/winheaders.h"

#include "client/HubWindow.h"

#include "QtWidgets/qapplication.h"

#include <packet.pb.h>


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	setupLogger();

	GOOGLE_PROTOBUF_VERIFY_VERSION;

	QApplication app(__argc, __argv);

	HubWindow hub;
	hub.showCentered();

	return app.exec();
}