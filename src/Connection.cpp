#include "Connection.h"
#include "Socket.h"
#include "Channel.h"
#include "Buffer.h"
#include "ThreadPool.h"
#include "MysqlPool.h"
#include "Config.h"
#include "Log.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <string>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <cctype>
#include <mutex>
#include <random>
#include <exception>

#ifdef USE_REDIS
#include "RedisPool.h"
#endif

Connection::Connection(EventLoop *loop, std::unique_ptr<Socket> sock, ThreadPool *dbPool,
                       std::function<void(int)> deleteCb)
    : loop_(loop), sock_(std::move(sock)), dbPool_(dbPool)
{
    // Set the delete callback BEFORE registering to epoll, so an event that
    // fires immediately after registration never hits an empty std::function.
    deleteConnectionCallback_ = std::move(deleteCb);
    lastActiveSec_.store((long)time(nullptr), std::memory_order_relaxed);
    channel_ = std::make_unique<Channel>(loop_, sock_->getFd());
    channel_->setCallback(std::bind(&Connection::handleMessage, this));
    channel_->setWriteCallback(std::bind(&Connection::handleWrite, this));
    channel_->enableReadingET();
}

Connection::~Connection()
{
    // fileFd_ (when set) is the shared cached static-file fd, owned by the
    // process-wide cache in getStaticFd() -- never closed per-connection.
}

// ==================== HTTP Parsing ====================

std::string Connection::parseMethod(const std::string &request)
{
    size_t pos = request.find(' ');
    if (pos == std::string::npos)
        return "GET";
    return request.substr(0, pos);
}

std::string Connection::parseUrl(const std::string &request)
{
    size_t pos1 = request.find(' ');
    if (pos1 == std::string::npos)
        return "/";
    size_t pos2 = request.find(' ', pos1 + 1);
    if (pos2 == std::string::npos)
        return "/";
    return request.substr(pos1 + 1, pos2 - pos1 - 1);
}

std::string Connection::parseBody(const std::string &request)
{
    size_t pos = request.find("\r\n\r\n");
    if (pos == std::string::npos)
        return "";
    return request.substr(pos + 4);
}

std::string Connection::getJsonValue(const std::string &json, const std::string &key)
{
    std::string search = "\"" + key + "\":\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos)
        return "";
    pos += search.size();
    size_t end = json.find('"', pos);
    if (end == std::string::npos)
        return "";
    return json.substr(pos, end - pos);
}

std::string Connection::parseBearerToken(const std::string &request)
{
    // Header field names are case-insensitive (RFC 9110); scan line by line
    // within the header section only, and normalize the token (trim + cap).
    size_t headerEnd = request.find("\r\n\r\n");
    std::string headers = (headerEnd == std::string::npos) ? request : request.substr(0, headerEnd);

    size_t lineStart = 0;
    while (lineStart < headers.size())
    {
        size_t lineEnd = headers.find("\r\n", lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = headers.size();
        std::string line = headers.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 2;

        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string name = line.substr(0, colon);
        for (char &c : name)
            c = (char)std::tolower((unsigned char)c);
        if (name != "authorization")
            continue;

        std::string value = line.substr(colon + 1);
        size_t b = value.find_first_not_of(" \t");
        if (b == std::string::npos)
            return "";
        value = value.substr(b);

        const std::string scheme = "bearer ";
        if (value.size() < scheme.size())
            return "";
        std::string prefix = value.substr(0, scheme.size());
        for (char &c : prefix)
            c = (char)std::tolower((unsigned char)c);
        if (prefix != scheme)
            return "";

        std::string token = value.substr(scheme.size());
        size_t e = token.find_last_not_of(" \t");
        if (e == std::string::npos)
            return "";
        token = token.substr(0, e + 1);
        // Cap length so a malicious header cannot build a huge Redis key
        if (token.size() > (size_t)config::kTokenLen * 2)
            return "";
        return token;
    }
    return "";
}

// Escape a string for safe inclusion inside a JSON string literal.
// Stored usernames are only length-limited, not charset-limited, so a value
// containing '"' or control chars would otherwise break or inject the JSON.
static std::string jsonEscape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if ((unsigned char)c < 0x20)
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                out += buf;
            }
            else
            {
                out += c;
            }
        }
    }
    return out;
}

// ==================== HTTP Response ====================

// Global fixed-window rate limiter (simple, coarse). Returns false when the
// current second's request count exceeds rate_limit_qps (0 = unlimited).
static bool allowRequest()
{
    int limit = config::kRateLimitQps;
    if (limit <= 0)
        return true;
    static std::atomic<long> windowSec{0};
    static std::atomic<int> count{0};
    long now = (long)time(nullptr);
    if (windowSec.load(std::memory_order_relaxed) != now)
    {
        windowSec.store(now, std::memory_order_relaxed);
        count.store(0, std::memory_order_relaxed);
    }
    return count.fetch_add(1, std::memory_order_relaxed) < limit;
}

std::string Connection::getResponse(const std::string &url)
{
    if (url == "/")
    {
        return "<html><body><h1>Welcome to My HTTP Server</h1>"
               "<p>This is a simple HTTP server built with epoll reactor pattern.</p>"
               "<p>Try: <a href=\"/hello\">/hello</a></p>"
               "<p>API: POST /register, POST /login, GET /profile, POST /logout</p>"
               "</body></html>";
    }
    else if (url == "/hello")
    {
        return "<html><body><h1>Hello!</h1></body></html>";
    }
    else
    {
        return "<html><body><h1>404 Not Found</h1></body></html>";
    }
}

std::string Connection::buildHttpResponse(int statusCode, const std::string &contentType,
                                          const std::string &body, bool keepAlive)
{
    std::string statusText;
    switch (statusCode)
    {
    case 200:
        statusText = "OK";
        break;
    case 400:
        statusText = "Bad Request";
        break;
    case 401:
        statusText = "Unauthorized";
        break;
    case 409:
        statusText = "Conflict";
        break;
    case 413:
        statusText = "Payload Too Large";
        break;
    case 429:
        statusText = "Too Many Requests";
        break;
    case 500:
        statusText = "Internal Server Error";
        break;
    case 503:
        statusText = "Service Unavailable";
        break;
    default:
        statusText = "Not Found";
        break;
    }

    // Content-Length is always present, so the client can frame the response
    // and reuse the connection when keep-alive is negotiated.
    return "HTTP/1.1 " + std::to_string(statusCode) + " " + statusText + "\r\n"
           "Content-Type: " + contentType + "\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: " + (keepAlive ? "keep-alive" : "close") + "\r\n\r\n" + body;
}

// Direct best-effort write from a DB worker thread (small JSON payloads).
// It does NOT touch the Channel/epoll (that belongs to the reactor thread),
// so on EAGAIN it can only stop; correct async write from worker threads would
// require posting the send back to the owning loop (a future improvement).
void Connection::sendHttpResponse(int fd, int statusCode, const std::string &contentType,
                                  const std::string &body, bool keepAlive)
{
    std::string response = buildHttpResponse(statusCode, contentType, body, keepAlive);
    const char *data = response.c_str();
    ssize_t total = response.size();
    ssize_t written = 0;
    while (written < total)
    {
        ssize_t n = write(fd, data + written, total - written);
        if (n > 0)
        {
            written += n;
        }
        else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            break;
        }
        else if (n == -1 && errno == EINTR)
        {
            continue;
        }
        else
        {
            break; // write error, fd may be closed
        }
    }
}

// Reactor-thread send: build the response into writeBuf_ and drain it.
// On partial write, EPOLLOUT (handleWrite) finishes the rest. When keepAlive
// is false the connection is closed once the buffer is fully sent.
void Connection::sendResponse(int statusCode, const std::string &contentType,
                              const std::string &body, bool keepAlive)
{
    writeBuf_ = buildHttpResponse(statusCode, contentType, body, keepAlive);
    writeIndex_ = 0;
    closeAfterWrite_ = !keepAlive;
    writeFromBuffer();
}

// Zero-copy static file: send headers via the write buffer, then the body via
// sendfile() (kernel copies file page cache straight to the socket, no user
// buffer). The file is opened ONCE and its fd cached: sendfile() takes an
// explicit offset and never touches the fd's own file position, so one shared
// read-only fd is safe to sendfile() concurrently from all reactor threads.
// This avoids a per-request open() syscall (which dominated latency and capped
// throughput ~32x on this filesystem). Returns false if the file is absent
// (caller falls back to the inline page).
static bool getStaticFd(const std::string &path, int &fdOut, off_t &sizeOut)
{
    static std::once_flag once;
    static int fd = -1;
    static off_t size = 0;
    std::call_once(once, [&]
                   {
        fd = ::open(path.c_str(), O_RDONLY);
        if (fd >= 0)
        {
            struct stat st;
            if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode))
                size = st.st_size;
            else { ::close(fd); fd = -1; }
        }
        if (fd >= 0)
            LOG_INFO("static file cached: %s (%lld bytes, fd=%d)", path.c_str(), (long long)size, fd);
        else
            LOG_WARN("static file open failed: %s (fallback to inline page)", path.c_str()); });
    fdOut = fd;
    sizeOut = size;
    return fd >= 0;
}

bool Connection::sendFile(int statusCode, const std::string &contentType,
                          const std::string &path, bool keepAlive)
{
    int fd;
    off_t size;
    if (!getStaticFd(path, fd, size)) // cached fd; no per-request open()
        return false;

    // Headers carry the real Content-Length so the client can frame the body.
    writeBuf_ = "HTTP/1.1 " + std::to_string(statusCode) + " OK\r\n"
                "Content-Type: " + contentType + "\r\n"
                "Content-Length: " + std::to_string((long long)size) + "\r\n"
                "Connection: " + (keepAlive ? "keep-alive" : "close") + "\r\n\r\n";
    writeIndex_ = 0;
    closeAfterWrite_ = !keepAlive;
    fileFd_ = fd; // borrowed shared fd -- must NOT be closed by this connection
    fileOffset_ = 0;
    fileRemaining_ = (size_t)size;
    writeFromBuffer();
    return true;
}

void Connection::writeFromBuffer()
{
    int fd = sock_->getFd();
    // 1) drain the header/body byte buffer
    while (writeIndex_ < writeBuf_.size())
    {
        ssize_t n = write(fd, writeBuf_.data() + writeIndex_, writeBuf_.size() - writeIndex_);
        if (n > 0)
            writeIndex_ += (size_t)n;
        else if (n == -1 && errno == EINTR)
            continue;
        else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            channel_->enableWriting();
            return;
        }
        else
        {
            deleteConnectionCallback_(fd);
            return; // 'this' destroyed
        }
    }

    // 2) zero-copy file body, if any
    while (fileFd_ >= 0 && fileRemaining_ > 0)
    {
        ssize_t s = sendfile(fd, fileFd_, &fileOffset_, fileRemaining_);
        if (s > 0)
            fileRemaining_ -= (size_t)s;
        else if (s == -1 && errno == EINTR)
            continue;
        else if (s == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            channel_->enableWriting();
            return;
        }
        else
        {
            deleteConnectionCallback_(fd);
            return; // 'this' destroyed
        }
    }
    // File body fully sent. fileFd_ is the shared cached fd -- do NOT close it,
    // just stop tracking it on this connection.
    fileFd_ = -1;

    // 3) fully sent
    writeBuf_.clear();
    writeIndex_ = 0;
    channel_->disableWriting();
    if (closeAfterWrite_)
    {
        // Connection: close negotiated -> actively close instead of waiting
        // for the peer's FIN (which otherwise leaks fds into CLOSE-WAIT).
        deleteConnectionCallback_(fd);
    }
}

void Connection::handleWrite()
{
    if (writeIndex_ < writeBuf_.size() || (fileFd_ >= 0 && fileRemaining_ > 0))
        writeFromBuffer();
    else
        channel_->disableWriting();
}

// ==================== DB Handlers ====================

static std::string generateToken()
{
    static const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    // Per-thread generator seeded from the system entropy source:
    // rand() is predictable (fixed seed) and racy across DB threads
    thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    std::string token;
    token.reserve(config::kTokenLen);
    for (int i = 0; i < config::kTokenLen; ++i)
    {
        token += charset[dist(rng)];
    }
    return token;
}

void Connection::handleRegister(const std::string &request, int fd, bool keepAlive)
{
    std::string body = parseBody(request);
    std::string username = getJsonValue(body, "username");
    std::string password = getJsonValue(body, "password");

    if (username.empty() || password.empty())
    {
        sendHttpResponse(fd, 400, "application/json", "{\"error\":\"missing username or password\"}", keepAlive);
        return;
    }

    // Enforce length limits: escape buffers below hold at most len*2+1 bytes
    // (username <= 64 -> esc_user[129], password <= 128 -> esc_pass[257])
    if (username.size() > config::kMaxUsernameLen || password.size() > config::kMaxPasswordLen)
    {
        sendHttpResponse(fd, 400, "application/json", "{\"error\":\"username or password too long\"}", keepAlive);
        return;
    }

    // Submit blocking DB work to the DB thread pool
    dbPool_->add([fd, keepAlive, username, password]()
                 {
        try
        {
            MysqlGuard guard;
            MYSQL *conn = guard.get();
            if (!conn)
            {
                sendHttpResponse(fd, 503, "application/json", "{\"error\":\"service unavailable\"}", keepAlive);
                return;
            }

            // Escape input to prevent SQL injection
            char esc_user[129], esc_pass[257];
            mysql_real_escape_string(conn, esc_user, username.c_str(), username.size());
            mysql_real_escape_string(conn, esc_pass, password.c_str(), password.size());

            // Check if user already exists
            std::string query = "SELECT 1 FROM users WHERE username='" + std::string(esc_user) + "' LIMIT 1";
            if (mysql_query(conn, query.c_str()))
            {
                sendHttpResponse(fd, 500, "application/json", "{\"error\":\"database error\"}", keepAlive);
                return;
            }

            MYSQL_RES *res = mysql_store_result(conn);
            if (res && mysql_num_rows(res) > 0)
            {
                mysql_free_result(res);
                sendHttpResponse(fd, 409, "application/json", "{\"error\":\"user already exists\"}", keepAlive);
                return;
            }
            if (res)
                mysql_free_result(res);

            // Insert new user
            std::string insert = "INSERT INTO users(username, password) VALUES('" +
                                 std::string(esc_user) + "','" + std::string(esc_pass) + "')";
            if (mysql_query(conn, insert.c_str()))
            {
                sendHttpResponse(fd, 500, "application/json", "{\"error\":\"database error\"}", keepAlive);
                return;
            }

            sendHttpResponse(fd, 200, "application/json", "{\"message\":\"register success\"}", keepAlive);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("[Register] exception: %s", e.what());
            sendHttpResponse(fd, 500, "application/json", "{\"error\":\"internal error\"}", keepAlive);
        }
        catch (...)
        {
            LOG_ERROR("[Register] unknown exception");
            sendHttpResponse(fd, 500, "application/json", "{\"error\":\"internal error\"}", keepAlive);
        } });
}

void Connection::handleLogin(const std::string &request, int fd, bool keepAlive)
{
    std::string body = parseBody(request);
    std::string username = getJsonValue(body, "username");
    std::string password = getJsonValue(body, "password");

    if (username.empty() || password.empty())
    {
        sendHttpResponse(fd, 400, "application/json", "{\"error\":\"missing username or password\"}", keepAlive);
        return;
    }

    // Enforce length limits: esc_user[129] below holds at most 64*2+1 bytes
    if (username.size() > config::kMaxUsernameLen || password.size() > config::kMaxPasswordLen)
    {
        sendHttpResponse(fd, 400, "application/json", "{\"error\":\"username or password too long\"}", keepAlive);
        return;
    }

    dbPool_->add([fd, keepAlive, username, password]()
                 {
        try
        {
        std::string dbPassword;
        bool found = false;

#ifdef USE_REDIS
        // Try Redis cache first. A Redis error here is non-fatal: we simply
        // fall through to MySQL (degraded, not failed).
        std::string cacheKey = "user:" + username;
        RedisStatus cacheSt;
        std::string cached = RedisPool::getInstance().get(cacheKey, &cacheSt);
        if (cacheSt == RedisStatus::Ok && !cached.empty())
        {
            dbPassword = cached;
            found = true;
        }
#endif

        if (!found)
        {
            // Cache miss, query MySQL
            MysqlGuard guard;
            MYSQL *conn = guard.get();
            if (!conn)
            {
                sendHttpResponse(fd, 503, "application/json", "{\"error\":\"service unavailable\"}", keepAlive);
                return;
            }

            char esc_user[129];
            mysql_real_escape_string(conn, esc_user, username.c_str(), username.size());

            std::string query = "SELECT password FROM users WHERE username='" +
                                std::string(esc_user) + "' LIMIT 1";
            if (mysql_query(conn, query.c_str()))
            {
                sendHttpResponse(fd, 500, "application/json", "{\"error\":\"database error\"}", keepAlive);
                return;
            }

            MYSQL_RES *res = mysql_store_result(conn);
            if (res && mysql_num_rows(res) > 0)
            {
                MYSQL_ROW row = mysql_fetch_row(res);
                dbPassword = row[0];
                found = true;
            }
            if (res)
                mysql_free_result(res);

#ifdef USE_REDIS
            // Write to Redis cache (TTL 300s)
            if (found)
            {
                RedisPool::getInstance().setex(cacheKey, config::kUserCacheTtlSec, dbPassword);
            }
#endif
        }

        if (!found)
        {
            sendHttpResponse(fd, 401, "application/json", "{\"error\":\"user not found\"}", keepAlive);
            return;
        }

        if (password != dbPassword)
        {
            sendHttpResponse(fd, 401, "application/json", "{\"error\":\"wrong password\"}", keepAlive);
            return;
        }

        // Login success, generate session token
        std::string token = generateToken();

#ifdef USE_REDIS
        // Store session in Redis (TTL 1h). If the store fails we must NOT hand
        // out a token that would never validate -> report 503 instead.
        if (!RedisPool::getInstance().setex("session:" + token, config::kSessionTtlSec, username))
        {
            sendHttpResponse(fd, 503, "application/json", "{\"error\":\"service unavailable\"}", keepAlive);
            return;
        }
#endif

        sendHttpResponse(fd, 200, "application/json",
                         "{\"message\":\"login success\",\"token\":\"" + token + "\"}", keepAlive);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("[Login] exception: %s", e.what());
            sendHttpResponse(fd, 500, "application/json", "{\"error\":\"internal error\"}", keepAlive);
        }
        catch (...)
        {
            LOG_ERROR("[Login] unknown exception");
            sendHttpResponse(fd, 500, "application/json", "{\"error\":\"internal error\"}", keepAlive);
        } });
}

void Connection::handleProfile(const std::string &request, int fd, bool keepAlive)
{
    std::string token = parseBearerToken(request);
    if (token.empty())
    {
        sendHttpResponse(fd, 401, "application/json", "{\"error\":\"missing token\"}", keepAlive);
        return;
    }

#ifdef USE_REDIS
    // Session lookup is a blocking Redis call, keep it off the reactor thread
    dbPool_->add([fd, keepAlive, token]()
                 {
        try
        {
            RedisStatus st;
            std::string username = RedisPool::getInstance().get("session:" + token, &st);
            if (st == RedisStatus::Error)
            {
                sendHttpResponse(fd, 503, "application/json", "{\"error\":\"service unavailable\"}", keepAlive);
                return;
            }
            if (st == RedisStatus::NotFound || username.empty())
            {
                sendHttpResponse(fd, 401, "application/json", "{\"error\":\"invalid or expired token\"}", keepAlive);
                return;
            }
            sendHttpResponse(fd, 200, "application/json",
                             "{\"username\":\"" + jsonEscape(username) + "\"}", keepAlive);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("[Profile] exception: %s", e.what());
            sendHttpResponse(fd, 500, "application/json", "{\"error\":\"internal error\"}", keepAlive);
        }
        catch (...)
        {
            LOG_ERROR("[Profile] unknown exception");
            sendHttpResponse(fd, 500, "application/json", "{\"error\":\"internal error\"}", keepAlive);
        } });
#else
    sendHttpResponse(fd, 503, "application/json", "{\"error\":\"session store disabled\"}", keepAlive);
#endif
}

void Connection::handleLogout(const std::string &request, int fd, bool keepAlive)
{
    std::string token = parseBearerToken(request);
    if (token.empty())
    {
        sendHttpResponse(fd, 401, "application/json", "{\"error\":\"missing token\"}", keepAlive);
        return;
    }

#ifdef USE_REDIS
    dbPool_->add([fd, keepAlive, token]()
                 {
        try
        {
            RedisStatus st = RedisPool::getInstance().del("session:" + token);
            if (st == RedisStatus::Error)
            {
                sendHttpResponse(fd, 503, "application/json", "{\"error\":\"service unavailable\"}", keepAlive);
                return;
            }
            if (st == RedisStatus::NotFound)
            {
                sendHttpResponse(fd, 401, "application/json", "{\"error\":\"invalid or expired token\"}", keepAlive);
                return;
            }
            sendHttpResponse(fd, 200, "application/json", "{\"message\":\"logout success\"}", keepAlive);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("[Logout] exception: %s", e.what());
            sendHttpResponse(fd, 500, "application/json", "{\"error\":\"internal error\"}", keepAlive);
        }
        catch (...)
        {
            LOG_ERROR("[Logout] unknown exception");
            sendHttpResponse(fd, 500, "application/json", "{\"error\":\"internal error\"}", keepAlive);
        } });
#else
    sendHttpResponse(fd, 503, "application/json", "{\"error\":\"session store disabled\"}", keepAlive);
#endif
}

// ==================== Main Handler ====================

// Content-Length of a request given its header block (text before \r\n\r\n).
// Header names are case-insensitive (RFC 9110); returns 0 if absent.
static size_t requestContentLength(const std::string &headers)
{
    size_t p = 0;
    while (p < headers.size())
    {
        size_t e = headers.find("\r\n", p);
        if (e == std::string::npos)
            e = headers.size();
        std::string line = headers.substr(p, e - p);
        p = e + 2;
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string name = line.substr(0, colon);
        for (char &c : name)
            c = (char)std::tolower((unsigned char)c);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
            name.pop_back();
        if (name == "content-length")
        {
            long v = strtol(line.c_str() + colon + 1, nullptr, 10);
            return v > 0 ? (size_t)v : 0;
        }
    }
    return 0;
}

// Decide keep-alive: HTTP/1.1 defaults to persistent unless "Connection: close";
// HTTP/1.0 defaults to close unless "Connection: keep-alive".
static bool wantKeepAlive(const std::string &request, const std::string &headers)
{
    bool http11 = request.find("HTTP/1.1") != std::string::npos;
    std::string connVal;
    size_t p = 0;
    while (p < headers.size())
    {
        size_t e = headers.find("\r\n", p);
        if (e == std::string::npos)
            e = headers.size();
        std::string line = headers.substr(p, e - p);
        p = e + 2;
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string name = line.substr(0, colon);
        for (char &c : name)
            c = (char)std::tolower((unsigned char)c);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
            name.pop_back();
        if (name == "connection")
        {
            connVal = line.substr(colon + 1);
            for (char &c : connVal)
                c = (char)std::tolower((unsigned char)c);
            break;
        }
    }
    if (connVal.find("close") != std::string::npos)
        return false;
    if (connVal.find("keep-alive") != std::string::npos)
        return true;
    return http11;
}

void Connection::handleMessage()
{
    int sockfd = sock_->getFd();
    lastActiveSec_.store((long)time(nullptr), std::memory_order_relaxed); // for idle sweep
    char buf[4096];
    // ET mode: drain the socket completely into the persistent input buffer.
    while (true)
    {
        ssize_t n = read(sockfd, buf, sizeof(buf));
        if (n > 0)
        {
            inputBuffer_.append(buf, (size_t)n);
        }
        else if (n == -1 && errno == EINTR)
        {
            continue;
        }
        else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            break; // fully drained
        }
        else
        {
            // n == 0 (peer closed) or hard read error -> tear down
            deleteConnectionCallback_(sockfd);
            return; // 'this' destroyed; must not touch members
        }
    }
    processRequests(sockfd);
}

// Frame and dispatch every complete request in inputBuffer_. Requests are
// delimited by \r\n\r\n plus Content-Length body, so a keep-alive connection
// can carry successive (even pipelined) requests.
// NOTE: after a non-keep-alive reactor send, sendResponse() may synchronously
// close and destroy 'this'; the only statement following such a send is a
// return that reads the local keepAlive flag (no member access) -> safe.
void Connection::processRequests(int fd)
{
    while (true)
    {
        size_t headerEnd = inputBuffer_.find("\r\n\r\n");
        if (headerEnd == std::string::npos)
        {
            // Header size guard: reject a header block that never terminates.
            size_t maxHdr = config::kMaxHeaderBytes;
            if (maxHdr > 0 && inputBuffer_.size() > maxHdr)
            {
                LOG_WARN("access - - -> 400 (header too large, %zu bytes)", inputBuffer_.size());
                inputBuffer_.clear();
                sendResponse(400, "application/json", "{\"error\":\"request header too large\"}", false);
                return; // closing
            }
            return; // headers incomplete; wait for the next read event
        }

        std::string headers = inputBuffer_.substr(0, headerEnd);
        size_t bodyLen = requestContentLength(headers);
        size_t maxBody = config::kMaxBodyBytes;
        if (maxBody > 0 && bodyLen > maxBody)
        {
            LOG_WARN("access - - -> 413 (body %zu > limit %zu)", bodyLen, maxBody);
            inputBuffer_.clear();
            sendResponse(413, "application/json", "{\"error\":\"request body too large\"}", false);
            return; // closing
        }

        size_t total = headerEnd + 4 + bodyLen;
        if (inputBuffer_.size() < total)
            return; // body not fully arrived yet

        std::string request = inputBuffer_.substr(0, total);
        inputBuffer_.erase(0, total);

        bool keepAlive = wantKeepAlive(request, headers);
        std::string method = parseMethod(request);
        std::string url = parseUrl(request);

        // Global rate limit: shed load with 429 before doing any work.
        if (!allowRequest())
        {
            LOG_WARN("access %s %s ka=%d -> 429 (rate limited)", method.c_str(), url.c_str(), keepAlive ? 1 : 0);
            sendResponse(429, "application/json", "{\"error\":\"too many requests\"}", keepAlive);
            if (!keepAlive) return;
            continue;
        }

        // POST routes + protected GET (async via DB thread pool)
        if (method == "POST" && url == "/register")
        {
            LOG_INFO("access %s %s ka=%d -> (async)", method.c_str(), url.c_str(), keepAlive ? 1 : 0);
            handleRegister(request, fd, keepAlive);
            if (!keepAlive) return;
            continue;
        }
        if (method == "POST" && url == "/login")
        {
            LOG_INFO("access %s %s ka=%d -> (async)", method.c_str(), url.c_str(), keepAlive ? 1 : 0);
            handleLogin(request, fd, keepAlive);
            if (!keepAlive) return;
            continue;
        }
        if (method == "POST" && url == "/logout")
        {
            LOG_INFO("access %s %s ka=%d -> (async)", method.c_str(), url.c_str(), keepAlive ? 1 : 0);
            handleLogout(request, fd, keepAlive);
            if (!keepAlive) return;
            continue;
        }
        if (method == "GET" && url == "/profile")
        {
            LOG_INFO("access %s %s ka=%d -> (async)", method.c_str(), url.c_str(), keepAlive ? 1 : 0);
            handleProfile(request, fd, keepAlive);
            if (!keepAlive) return;
            continue;
        }

        // GET inline routes (reactor thread). "/" is served zero-copy via
        // sendfile from static/index.html; if that file is absent, fall back
        // to the inline welcome page.
        if (url == "/")
        {
            if (sendFile(200, "text/html", config::staticDir() + "/index.html", keepAlive))
            {
                LOG_INFO("access GET / ka=%d -> 200 (sendfile)", keepAlive ? 1 : 0);
                if (!keepAlive) return; // may have closed 'this'
                continue;
            }
        }
        std::string rbody = getResponse(url);
        int statusCode = (url == "/" || url == "/hello") ? 200 : 404;
        LOG_INFO("access %s %s ka=%d -> %d", method.c_str(), url.c_str(), keepAlive ? 1 : 0, statusCode);
        sendResponse(statusCode, "text/html", rbody, keepAlive);
        if (!keepAlive) return; // may have closed 'this'
    }
}
