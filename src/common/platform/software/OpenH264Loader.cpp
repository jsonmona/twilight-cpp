#include "OpenH264Loader.h"

#include <openh264/codec_ver.h>

#ifdef WIN32
#include "common/platform/windows/OpenH264LoaderWin32.h"
#else
#error OpenH264 Unsupported platform
#endif

TWILIGHT_DEFINE_LOGGER(OpenH264Loader);

// static member out-of-class definition
std::weak_ptr<OpenH264Loader> OpenH264Loader::instance;
std::mutex OpenH264Loader::instanceLock;

OpenH264Loader::OpenH264Loader() {}
OpenH264Loader::~OpenH264Loader() {}

std::shared_ptr<OpenH264Loader> OpenH264Loader::getInstance() {
    // Opportunistic read
    auto ptr = instance.lock();
    if (ptr != nullptr)
        return ptr;

    std::lock_guard lock(instanceLock);

    // Try again with mutex this time
    ptr = instance.lock();
    if (ptr != nullptr)
        return ptr;

    ptr = std::make_shared<OpenH264LoaderWin32>();
    instance = ptr;
    return ptr;
}

bool OpenH264Loader::checkVersion() const {
    OpenH264Version version = {};
    GetCodecVersionEx(&version);
    log.info("Compiled with API for {}", g_strCodecVer);
    log.info("Loaded OpenH264 {}.{}.{}", version.uMajor, version.uMinor, version.uRevision);

    if (version.uMajor != OPENH264_MAJOR || version.uMinor < OPENH264_MINOR ||
        (version.uMinor == OPENH264_MINOR && version.uRevision < OPENH264_REVISION))
        log.error_quit("OpenH264 loaded is incompatible with API!");

    return true;
}
