#include "common/log.h"

#include "common/platform/windows/ComWrapper.h"

#include "server/StreamServer.h"

#include "server/platform/windows/CaptureD3D.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

typedef BOOL(WINAPI *FnSetProcessDpiAwarenessContext)(HANDLE);
static const HANDLE dpiAwarenessContextPerMonitorAwareV2 = reinterpret_cast<HANDLE>(0xfffffffffffffffc);

static bool setDpiAwareness() {
    bool success = false;

    HMODULE user32 = LoadLibrary(L"user32.dll");
    if (user32 != nullptr) {
        auto fn = (FnSetProcessDpiAwarenessContext)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn != nullptr)
            success = fn(dpiAwarenessContextPerMonitorAwareV2);
        FreeLibrary(user32);
        user32 = nullptr;
    }

    if (!success)
        success = SetProcessDPIAware();

    return success;
}

int main() {
    setupLogger();

    GOOGLE_PROTOBUF_VERIFY_VERSION;
    HRESULT hr;

    NamedLogger log("name");

    if (!setDpiAwareness())
        log.warn("Unable to set dpi awareness");

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    log.assert_quit(SUCCEEDED(hr), "Failed to initialize COM");

    hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    log.assert_quit(SUCCEEDED(hr), "Failed to start MediaFoundation");

    StreamServer stream;

    log.info("Starting twilight remote desktop server...");

    stream.start();
    Sleep(3600 * 1000);

    log.info("Stopping twilight remote desktop server...");
    stream.stop();

    MFShutdown();
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return main();
}
