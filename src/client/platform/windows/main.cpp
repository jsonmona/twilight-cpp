#include "common/log.h"
#include "common/platform/windows/winheaders.h"
#include "client/StreamViewer.h"

#include <packets.pb.h>

#include <QtWidgets/qapplication.h>
#include <QtGui/qscreen.h>
#include <cstdio>


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
	auto default_log = std::make_shared<spdlog::logger>("default", msvc_sink);
	spdlog::set_default_logger(default_log);

	FILE* f = fopen("../server-stream.dump", "rb");
	fseek(f, 0, SEEK_END);
	long fileLen = ftell(f);
	fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> buf(fileLen);
	int ret = fread(buf.data(), 1, fileLen, f);
	if(ret != fileLen)
		abort();

	QApplication app(__argc, __argv);

	QRect screenSize = app.primaryScreen()->geometry();

	int targetWidth = screenSize.width() * 5 / 6;
	int targetHeight = screenSize.height() * 5 / 6;

	StreamViewer sv;
	sv.data = buf;
	sv.dataPos = 0;
	sv.setFixedSize(QSize(targetWidth, targetHeight));
	sv.show();

	return app.exec();
}