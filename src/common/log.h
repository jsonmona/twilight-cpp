#ifndef TWILIGHT_COMMON_LOG_H
#define TWILIGHT_COMMON_LOG_H

// clang-format off

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4819)
#endif

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

// clang-format on

#include <string>
#include <type_traits>
#include <utility>

struct fatal_error {};

// Forward logs from various libraries.
// Call once at start
void setupLogger();

// Define static member variable declared like `static NamedLogger log` as `NamedLogger ClassName::log("ClassName");`
#define TWILIGHT_DEFINE_LOGGER(CLS) NamedLogger CLS ::log(#CLS)

#if defined(_MSC_VER) && defined(_PREFAST_)
// Mess with assert_quit to make MSVC static code analyzer work.
// It seems there is no SAL to annotate assert-like feature.
// Also see bottom of this file
#define TWILIGHT_MESSING_WITH_ASSERTION
#define assert_quit assert_quit_real_impl
#else
#endif

// Include as class static member variable
class NamedLogger {
public:
    explicit NamedLogger(const char *name) : name(name) {}
    NamedLogger(const NamedLogger &copy) = default;
    NamedLogger(NamedLogger &&move) = default;

    bool should_log(spdlog::level::level_enum lvl) const { return spdlog::should_log(lvl); }

    // Simple message logs

    void critical(std::string_view msg) const { log(spdlog::level::critical, msg); }
    void error(std::string_view msg) const { log(spdlog::level::err, msg); }
    void warn(std::string_view msg) const { log(spdlog::level::warn, msg); }
    void info(std::string_view msg) const { log(spdlog::level::info, msg); }
    void debug(std::string_view msg) const { log(spdlog::level::debug, msg); }

    // Simple message quitters

    [[noreturn]] void error_quit(std::string_view msg) const {
        critical(msg);
        throw fatal_error();
    }

    void assert_quit(bool assertion, std::string_view msg) const {
        if (assertion)
            return;
        else
            error_quit(msg);
    }

    // Plain loggers

    void log(spdlog::level::level_enum lvl, std::string_view msg) const {
        if (!spdlog::should_log(lvl))
            return;
        spdlog::log(lvl, "[twilight] {}: {}", name, msg);
    }

    template <typename... Args>
    void log(spdlog::level::level_enum lvl, std::string_view pattern, Args &&...args) const {
        if (!spdlog::should_log(lvl))
            return;
        std::string ptrn = fmt::format("[twilight] {}: {}", name, pattern);
        spdlog::log(lvl, ptrn, std::forward<Args>(args)...);
    }

    // Logs with arguments

    template <typename... Args>
    void critical(std::string_view msg, Args &&...args) const {
        log(spdlog::level::critical, msg, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(std::string_view msg, Args &&...args) const {
        log(spdlog::level::err, msg, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(std::string_view msg, Args &&...args) const {
        log(spdlog::level::warn, msg, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void info(std::string_view msg, Args &&...args) const {
        log(spdlog::level::info, msg, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void debug(std::string_view msg, Args &&...args) const {
        log(spdlog::level::debug, msg, std::forward<Args>(args)...);
    }

    // Quitters with argument

    template <typename... Args>
    [[noreturn]] void error_quit(std::string_view msg, Args &&...args) const {
        critical(msg, std::forward<Args>(args)...);
        throw fatal_error();
    }

    template <typename... Args>
    void assert_quit(bool assertion, std::string_view msg, Args &&...args) const {
        if (assertion)
            return;
        else
            error_quit(msg, std::forward<Args>(args)...);
    }

private:
    const char *name;
};

#ifdef TWILIGHT_MESSING_WITH_ASSERTION
#undef TWILIGHT_MESSING_WITH_ASSERTION
#undef assert_quit
#define assert_quit(X, ...) assert_quit_real_impl((X), __VA_ARGS__), __analyse_assume(X)
#endif

// Wrapper for mbedtls error code
// To be used in formatter argument
struct mbedtls_error {
    int errnum;
};

// Stringify mbedtls error
std::string interpretMbedtlsError(mbedtls_error err);

template <>
struct fmt::formatter<mbedtls_error> {
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const mbedtls_error &input, FormatContext &ctx) -> decltype(ctx.out()) {
        return format_to(ctx.out(), "{}", interpretMbedtlsError(input));
    }
};

#endif
