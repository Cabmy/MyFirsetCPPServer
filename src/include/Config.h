#pragma once
// ==================== Central configuration ====================
// All tunables for the server live here. Host/credential settings can be
// overridden at startup via environment variables (no rebuild needed) --
// useful because the WSL NAT gateway IP may change across reboots.
//
//   MYSERVER_HOST / MYSERVER_PORT           HTTP listen address
//   MYSERVER_DB_HOST / MYSERVER_DB_PORT     MySQL address
//   MYSERVER_DB_USER / MYSERVER_DB_PASSWD   MySQL credentials
//   MYSERVER_DB_NAME                        MySQL database
//   MYSERVER_REDIS_HOST / MYSERVER_REDIS_PORT  Redis address

#include <cstdlib>
#include <cstdio>
#include <string>

namespace config
{

// ---- env helpers ----
inline std::string envOr(const char *name, const char *defval)
{
    const char *v = std::getenv(name);
    return (v && *v) ? v : defval;
}

// Parse an integer env var; on missing/invalid value warn and fall back.
// atoi silently yields 0 on garbage (e.g. bind to a random port), so validate.
inline int envOrInt(const char *name, int defval)
{
    const char *v = std::getenv(name);
    if (!v || !*v)
        return defval;
    char *end = nullptr;
    long n = std::strtol(v, &end, 10);
    if (*end != '\0' || n <= 0)
    {
        std::fprintf(stderr, "config: invalid %s='%s', using default %d\n", name, v, defval);
        return defval;
    }
    return (int)n;
}

// ---- HTTP server ----
inline std::string serverHost() { return envOr("MYSERVER_HOST", "127.0.0.1"); }
inline int serverPort() { return envOrInt("MYSERVER_PORT", 8888); }

// Directory holding the demo frontend (index.html); default assumes the
// server is started from build/. Override with MYSERVER_STATIC_DIR.
inline std::string staticDir() { return envOr("MYSERVER_STATIC_DIR", "../static"); }

// DB thread pool size (blocking MySQL/Redis work)
constexpr int kDbThreads = 4;

// ---- MySQL ----
// Default is loopback. NOTE: if MySQL runs on the Windows host under WSL NAT
// mode, reach it via the NAT gateway IP (see `ip route show default`) by
// exporting MYSERVER_DB_HOST -- do NOT bake a machine-specific IP as default.
inline std::string dbHost() { return envOr("MYSERVER_DB_HOST", "127.0.0.1"); }
inline std::string dbUser() { return envOr("MYSERVER_DB_USER", "root"); }
inline std::string dbPasswd() { return envOr("MYSERVER_DB_PASSWD", "admin"); }
inline std::string dbName() { return envOr("MYSERVER_DB_NAME", "myserver"); }
inline unsigned int dbPort() { return (unsigned int)envOrInt("MYSERVER_DB_PORT", 3306); }
constexpr int kDbMaxConn = 8;

// ---- Redis ----
// Default is loopback. NOTE: under WSL NAT mode the IPv4 loopback source may
// be mangled so Redis protected mode DENIES 127.0.0.1; if so export
// MYSERVER_REDIS_HOST=::1 (IPv6 loopback is recognized).
inline std::string redisHost() { return envOr("MYSERVER_REDIS_HOST", "127.0.0.1"); }
inline unsigned int redisPort() { return (unsigned int)envOrInt("MYSERVER_REDIS_PORT", 6379); }
constexpr int kRedisMaxConn = 8;
constexpr int kRedisConnectTimeoutMs = 1500;

// Both pools: max seconds to wait for a free connection before
// giving up (getConn returns nullptr -> caller degrades to HTTP 503)
constexpr int kPoolAcquireTimeoutSec = 5;

// ---- Business rules (Connection.cpp) ----
// Length limits match users table (VARCHAR(64)/VARCHAR(128)) and size
// the mysql_real_escape_string buffers (len*2+1)
constexpr size_t kMaxUsernameLen = 64;
constexpr size_t kMaxPasswordLen = 128;
constexpr int kUserCacheTtlSec = 300;   // Redis "user:" password cache
constexpr int kSessionTtlSec = 3600;    // Redis "session:" token TTL
constexpr int kTokenLen = 32;

// ---- Connection limits ----
constexpr int kIdleTimeoutSec = 60;               // close a connection idle this many seconds (0 = never)
constexpr size_t kMaxHeaderBytes = 16 * 1024;     // reject request headers larger than this (0 = unlimited)
constexpr size_t kMaxBodyBytes = 1 * 1024 * 1024; // reject request body larger than this (0 = unlimited)
constexpr int kRateLimitQps = 0;                  // global max requests/sec (0 = unlimited)

} // namespace config
