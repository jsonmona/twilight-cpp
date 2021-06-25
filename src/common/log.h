#ifndef COMMON_LOG_H_
#define COMMON_LOG_H_


#ifdef _MSC_VER
	// Disable "Save the file in unicode format" warning
	#pragma warning(push)
	#pragma warning(disable: 4819)
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/msvc_sink.h>

#ifdef _MSC_VER
	#pragma warning(pop)
#endif

#include <type_traits>
#include <memory>
#include <utility>

struct fatal_error {};


using LoggerPtr = std::shared_ptr<spdlog::logger>;

// Create a named logger with same sink as default one
template<typename T>
LoggerPtr createNamedLogger(const T& name) {
	auto ptr = spdlog::get(name);
	if (ptr != nullptr)
		return ptr;
	auto logger = spdlog::default_logger();
	auto& sinks = logger->sinks();
	ptr = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
	spdlog::register_logger(ptr);
	return ptr;
}

// error_quit

template<typename Logger, typename T>
[[noreturn]] inline void error_quit(std::shared_ptr<Logger> logger, const T& msg) {
	logger->error(msg);
	logger->flush();
	throw fatal_error();
}

template<typename Logger, typename T>
[[noreturn]] inline void error_quit(Logger& logger, const T& msg) {
	logger.error(msg);
	logger.flush();
	throw fatal_error();
}

template<typename Logger, typename FormatString, typename... Args>
[[noreturn]] inline void error_quit(std::shared_ptr<Logger> logger, const FormatString& fmt, Args &&...args) {
	logger->error(fmt, std::forward<Args>(args)...);
	logger->flush();
	throw fatal_error();
}

template<typename Logger, typename FormatString, typename... Args>
[[noreturn]] inline void error_quit(Logger& logger, const FormatString& fmt, Args &&...args) {
	logger->error(fmt, std::forward<Args>(args)...);
	logger->flush();
	throw fatal_error();
}


// check_quit

template<typename Logger, typename T>
inline void check_quit(bool cond, std::shared_ptr<Logger> logger, const T& msg) {
	if (cond)
		error_quit(logger, msg);
}

template<typename Logger, typename T>
inline void check_quit(bool cond, Logger& logger, const T& msg) {
	if (cond)
		error_quit(logger, msg);
}

template<typename Logger, typename FormatString, typename... Args>
inline void check_quit(bool cond, std::shared_ptr<Logger> logger, const FormatString& fmt, Args &&...args) {
	if (cond)
		error_quit(logger, fmt, std::forward<Args>(args)...);
}

template<typename Logger, typename FormatString, typename... Args>
inline void check_quit(bool cond, Logger& logger, const FormatString& fmt, Args &&...args) {
	if (cond)
		error_quit(logger, fmt, std::forward<Args>(args)...);
}


#endif