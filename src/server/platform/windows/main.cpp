#include "stdafx.h"

#include "common/platform/windows/ComWrapper.h"
#include "StreamManager.h"

#include <memory>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <chrono>


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	//TODO: Use manifest
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
		MessageBox(nullptr, L"Failed to initialize COM!", nullptr, 0);
		abort();
	}
	if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
		MessageBox(nullptr, L"Failed to start Media Foundation!", nullptr, 0);
		abort();
	}

	WSADATA wsaData;
	SOCKET ConnectSocket = INVALID_SOCKET;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	SOCKADDR_IN target;
	target.sin_family = AF_INET;
	target.sin_port = htons(5678);
	target.sin_addr.s_addr = inet_addr("127.0.0.1");

	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Use ffplay -analyzeduration 1 -fflags -nobuffer -probesize 32 -sync ext -f h264 -vf scale=-1:ih/2 tcp://127.0.0.1:5678?listen=1
	auto startTime = std::chrono::steady_clock::now();
	int ret = WSAECONNREFUSED;
	while (ret != 0) {
		if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count() > 5)
			break;
		Sleep(1);
		ret = connect(s, (SOCKADDR*)&target, sizeof(target));
	}
	
	if (ret != 0)
		abort();  // Did not connect

	StreamManager streamManager([&](void* _data, const VideoFrame& frame) {
		send(s, (const char*)frame.desktopImage.data(), frame.desktopImage.size(), 0);
	}, nullptr);

	streamManager.start();
	Sleep(60 * 1000);
	streamManager.stop();

	MFShutdown();
}