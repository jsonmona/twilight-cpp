#include "log.h"

#include <spdlog/pattern_formatter.h>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/wincolor_sink.h>


class LogForwardingSink : public spdlog::sinks::base_sink<spdlog::details::null_mutex> {
	using Super = spdlog::sinks::base_sink<spdlog::details::null_mutex>;

public:
	LogForwardingSink(LoggerPtr log) : m_log(log) {}

protected:
	void sink_it_(const spdlog::details::log_msg& msg) override {
		if (!m_log->should_log(msg.level))
			return;

		spdlog::memory_buf_t formatted;
		Super::formatter_->format(msg, formatted);
		std::string str = fmt::to_string(formatted);

		m_log->log(msg.time, msg.source, msg.level, str);
	}

	void flush_() override {
		m_log->flush();
	}

private:
	LoggerPtr m_log;
};


LoggerPtr getGlobalLogger() {
	static std::mutex createLoggerLock;

	LoggerPtr ptr = spdlog::get("daylight");
	if (ptr != nullptr)
		return ptr;

	std::lock_guard lock(createLoggerLock);

	ptr = spdlog::get("daylight");
	if (ptr != nullptr)
		return ptr;

	std::vector<spdlog::sink_ptr> sinks;
	sinks.reserve(2);
	sinks.emplace_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
	sinks.emplace_back(std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>(spdlog::color_mode::automatic));

	ptr = std::make_shared<spdlog::logger>("daylight", sinks.begin(), sinks.end());
	spdlog::register_logger(ptr);
	spdlog::set_default_logger(ptr);

	return ptr;
}

LoggerPtr createNamedLogger(const std::string& tag) {
	static std::mutex createLoggerLock;

	std::string name = "daylight::" + tag;

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
	sink->set_formatter(std::make_unique<spdlog::pattern_formatter>(std::move(ptrn), spdlog::pattern_time_type::local, ""));

	ptr = std::make_shared<spdlog::logger>(name, sink);
	spdlog::register_logger(ptr);

	return ptr;
}