#ifndef COMMON_LOG_H_
#define COMMON_LOG_H_


#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4819)
#endif

#include <spdlog/spdlog.h>

#ifdef _MSC_VER
#pragma warning (pop)
#endif

#include <type_traits>
#include <string>
#include <memory>
#include <utility>

struct fatal_error {};


using LoggerPtr = std::shared_ptr<spdlog::logger>;

// Create a global logger
LoggerPtr getGlobalLogger();

// Create a named logger and forward it to global logger
LoggerPtr createNamedLogger(const std::string& name);

// Forward logs from various libraries.
// Call once at start
void setupLogger();

// Stringify mbedtls errors
std::string interpretMbedtlsError(int errnum);


// error_quit

[[noreturn]] inline void error_quit(spdlog::logger& logger, std::string_view msg) {
	logger.critical(msg);
	logger.flush();
	throw fatal_error();
}

[[noreturn]] inline void error_quit(const LoggerPtr& logger, std::string_view msg) {
	logger->critical(msg);
	logger->flush();
	throw fatal_error();
}

template<typename... Args>
[[noreturn]] inline void error_quit(spdlog::logger& logger, std::string_view fmt, Args &&...args) {
	logger.critical(fmt, std::forward<Args>(args)...);
	logger.flush();
	throw fatal_error();
}

template<typename... Args>
[[noreturn]] inline void error_quit(const LoggerPtr& logger, std::string_view fmt, Args &&...args) {
	logger->critical(fmt, std::forward<Args>(args)...);
	logger->flush();
	throw fatal_error();
}


// check_quit

inline void check_quit(bool cond, spdlog::logger& logger, std::string_view msg) {
	if (cond)
		error_quit(logger, msg);
}

inline void check_quit(bool cond, const LoggerPtr& logger, std::string_view msg) {
	if (cond)
		error_quit(logger, msg);
}

template<typename... Args>
inline void check_quit(bool cond, spdlog::logger& logger, std::string_view fmt, Args &&...args) {
	if (cond)
		error_quit(logger, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
inline void check_quit(bool cond, const LoggerPtr& logger, std::string_view fmt, Args &&...args) {
	if (cond)
		error_quit(logger, fmt, std::forward<Args>(args)...);
}


#endif