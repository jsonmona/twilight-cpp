#include "common/platform/windows/winheaders.h"
#include "client/StreamViewer.h"

#include <QtWidgets/qapplication.h>
#include <cstdio>


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	QApplication app(__argc, __argv);

	StreamViewer sv;

	sv.show();

	return app.exec();
}