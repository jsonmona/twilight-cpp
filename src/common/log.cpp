#include "log.h"

#include "common/ffmpeg-headers.h"

#include <spdlog/pattern_formatter.h>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/wincolor_sink.h>

#include <mbedtls/error.h>

#include <cstdarg>
#include <cstring>

static void libav_log_sink(void *_obj, int level, const char *format, va_list vl) {
    static std::mutex logLock;
    static char buf[4096] = "";
    static NamedLogger logger = NamedLogger("ffmpeg");

    spdlog::level::level_enum lv;
    if (level <= AV_LOG_FATAL)
        lv = spdlog::level::critical;
    else if (level <= AV_LOG_ERROR)
        lv = spdlog::level::err;
    else if (level <= AV_LOG_WARNING)
        lv = spdlog::level::warn;
    else if (level <= AV_LOG_INFO)
        lv = spdlog::level::info;
    else
        lv = spdlog::level::debug;

    if (logger.should_log(lv)) {
        std::lock_guard lock(logLock);
        int len = vsnprintf(buf, 4095, format, vl);

        // Strip newline
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = '\0';

        logger.log(lv, std::string_view(buf, len));
    }
}

static void setupFFmpegLogs() {
    av_log_set_callback(libav_log_sink);
}

void setupLogger() {
    static std::mutex logSetupLock;
    static bool initialized;

    std::lock_guard lock(logSetupLock);

    NamedLogger log("setupLogger");

    log.assert_quit(!initialized, "Loggers already initialized!");
    initialized = true;

    if (spdlog::get("twilight") != nullptr) {
        log.error("Logger already exists!");
    } else {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.reserve(2);
        sinks.emplace_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
        sinks.emplace_back(std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>(spdlog::color_mode::automatic));

        auto ptr = std::make_shared<spdlog::logger>("twilight", sinks.begin(), sinks.end());
        spdlog::register_logger(ptr);
        spdlog::set_default_logger(ptr);
    }

    setupFFmpegLogs();
}

std::string interpretMbedtlsError(mbedtls_error err) {
    std::string ret;
    ret.resize(256);

    mbedtls_strerror(err.errnum, ret.data(), ret.size());
    ret.resize(strlen(ret.data()));
    return ret;
}
