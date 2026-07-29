#include "Log.h"
#include "Config.h"

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <memory>
#include <mutex>

namespace logx
{
namespace
{
// Lazily create the shared async spdlog logger on first use.
// Async mode = background thread + lock-free queue: producer threads enqueue
// and return, never blocking on the sink's I/O.
std::shared_ptr<spdlog::logger> makeLogger()
{
    // 8192-slot queue, 1 dedicated background worker thread.
    spdlog::init_thread_pool(8192, 1);

    std::vector<spdlog::sink_ptr> sinks;
    std::string file = config::envOr("MYSERVER_LOG_FILE", "");
    if (!file.empty())
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            file, 10 * 1024 * 1024, 5)); // 10MB x 5 rotated files
    else
        sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());

    auto logger = std::make_shared<spdlog::async_logger>(
        "server", sinks.begin(), sinks.end(),
        spdlog::thread_pool(), spdlog::async_overflow_policy::block);

    // time.us [level] thread message
    logger->set_pattern("%H:%M:%S.%f [%^%L%$] %t %v");
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::warn);
    // Periodic flush so file-sink logs are durable even without clean shutdown
    // (async buffered lines would otherwise be lost on SIGTERM).
    spdlog::flush_every(std::chrono::seconds(1));
    spdlog::register_logger(logger);
    return logger;
}

spdlog::logger &get()
{
    static std::shared_ptr<spdlog::logger> logger = makeLogger();
    return *logger;
}

const char *baseName(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}
} // namespace

void logf(Level level, const char *file, int line, const char *fmt, ...)
{
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;

    // Prepend "file:line: " then hand one preformatted line to spdlog.
    // Passed as a runtime argument (not the fmt string) so any '{' or '%' in
    // the message can never be interpreted as a format token.
    switch (level)
    {
    case Level::Info:
        get().info("{}:{}: {}", baseName(file), line, msg);
        break;
    case Level::Warn:
        get().warn("{}:{}: {}", baseName(file), line, msg);
        break;
    case Level::Error:
        get().error("{}:{}: {}", baseName(file), line, msg);
        break;
    }
}
} // namespace logx
