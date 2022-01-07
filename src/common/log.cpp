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

class LogForwardingSink : public spdlog::sinks::base_sink<spdlog::details::null_mutex> {
    using Super = spdlog::sinks::base_sink<spdlog::details::null_mutex>;

public:
    LogForwardingSink(LoggerPtr log) : m_log(log) {}

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override {
        if (!m_log->should_log(msg.level))
            return;

        spdlog::memory_buf_t formatted;
        Super::formatter_->format(msg, formatted);
        std::string str = fmt::to_string(formatted);

        m_log->log(msg.time, msg.source, msg.level, str);
    }

    void flush_() override { m_log->flush(); }

private:
    LoggerPtr m_log;
};

LoggerPtr getGlobalLogger() {
    static std::mutex createLoggerLock;

    LoggerPtr ptr = spdlog::get("twilight");
    if (ptr != nullptr)
        return ptr;

    std::lock_guard lock(createLoggerLock);

    ptr = spdlog::get("twilight");
    if (ptr != nullptr)
        return ptr;

    std::vector<spdlog::sink_ptr> sinks;
    sinks.reserve(2);
    sinks.emplace_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
    sinks.emplace_back(std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>(spdlog::color_mode::automatic));

    ptr = std::make_shared<spdlog::logger>("twilight", sinks.begin(), sinks.end());
    spdlog::register_logger(ptr);
    spdlog::set_default_logger(ptr);

    return ptr;
}

LoggerPtr createNamedLogger(const std::string &tag) {
    static std::mutex createLoggerLock;

    std::string name = "twilight::" + tag;

    LoggerPtr ptr = spdlog::get(name);
    if (ptr != nullptr)
        return ptr;

    std::lock_guard lock(createLoggerLock);

    ptr = spdlog::get(name);
    if (ptr != nullptr)
        return ptr;

    std::string ptrn;
    ptrn.reserve(tag.size() + 8);
    ptrn += "[";
    ptrn += tag;
    ptrn += "] %v";

    auto sink = std::make_shared<LogForwardingSink>(getGlobalLogger());
    sink->set_formatter(
        std::make_unique<spdlog::pattern_formatter>(std::move(ptrn), spdlog::pattern_time_type::local, ""));

    ptr = std::make_shared<spdlog::logger>(name, sink);
    spdlog::register_logger(ptr);

    return ptr;
}

static void libav_log_sink(void *_obj, int level, const char *format, va_list vl) {
    static std::mutex logLock;
    static char buf[4096] = "";

    auto globalLogger = createNamedLogger("ffmpeg");
    spdlog::level::level_enum lv;
    if (level < 0)
        return;
    else if (level <= AV_LOG_FATAL)
        lv = spdlog::level::critical;
    else if (level <= AV_LOG_ERROR)
        lv = spdlog::level::err;
    else if (level <= AV_LOG_WARNING)
        lv = spdlog::level::warn;
    else if (level <= AV_LOG_INFO)
        lv = spdlog::level::info;
    else if (level <= AV_LOG_VERBOSE)
        lv = spdlog::level::debug;
    else
        return;

    if (globalLogger->should_log(lv)) {
        std::lock_guard lock(logLock);
        int len = vsnprintf(buf, 4095, format, vl);

        // Strip newline
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = '\0';

        globalLogger->log(lv, std::string_view(buf, len));
    }
}

static void setupFFmpegLogs() {
    av_log_set_callback(libav_log_sink);
}

void setupLogger() {
    setupFFmpegLogs();
}

std::string interpretMbedtlsError(int errnum) {
    std::string ret;
    ret.resize(2048);

    mbedtls_strerror(errnum, ret.data(), ret.size());
    ret.resize(strlen(ret.data()));
    return ret;
}