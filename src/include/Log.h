#pragma once
// ==================== Logging (backed by spdlog) ====================
// Thin printf-style adapter over spdlog's asynchronous logger (background
// thread + lock-free queue). Call sites keep using LOG_INFO/WARN/ERROR with
// printf-style format specifiers; the engine (async queue, sinks, level
// filtering, timestamps, optional file rotation) is provided by spdlog.
//
// Output: MYSERVER_LOG_FILE if set (rotating file), otherwise stderr.

namespace logx
{
enum class Level
{
    Info,
    Warn,
    Error
};

// printf-style; formats into a local buffer then hands one line to spdlog.
void logf(Level level, const char *file, int line, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;
} // namespace logx

#define LOG_INFO(...) ::logx::logf(::logx::Level::Info, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) ::logx::logf(::logx::Level::Warn, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) ::logx::logf(::logx::Level::Error, __FILE__, __LINE__, __VA_ARGS__)
