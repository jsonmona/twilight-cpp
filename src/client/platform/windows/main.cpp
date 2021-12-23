#include <QtCore/qpointer.h>
#include <packet.pb.h>

#include "QtWidgets/qapplication.h"
#include "client/HubWindow.h"
#include "common/log.h"
#include "common/platform/windows/winheaders.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    setupLogger();

    GOOGLE_PROTOBUF_VERIFY_VERSION;

    QApplication app(__argc, __argv);

    QPointer<HubWindow> hub = new HubWindow();
    hub->showCentered();

    return app.exec();
}