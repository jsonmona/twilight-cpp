#include "common/log.h"
#include "common/platform/windows/winheaders.h"
#include "client/StreamViewer.h"
#include "client/StreamClient.h"

#include <packet.pb.h>

#include <QtWidgets/qapplication.h>
#include <QtGui/qscreen.h>
#include <cstdio>


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
	auto default_log = std::make_shared<spdlog::logger>("default", msvc_sink);
	spdlog::set_default_logger(default_log);

	StreamClient sc;

	QApplication app(__argc, __argv);

	QRect screenSize = app.primaryScreen()->geometry();

	int targetWidth = screenSize.width() * 5 / 6;
	int targetHeight = screenSize.height() * 5 / 6;

	StreamViewer sv;
	sc.setOnNextPacket([&](const msg::Packet& pkt, uint8_t* extraData) { sv.onNewPacket(pkt, extraData); });
	sc.connect("169.254.28.215");
	sv.setFixedSize(QSize(targetWidth, targetHeight));
	sv.show();

	return app.exec();
}