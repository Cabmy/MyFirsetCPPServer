#include "Connection.h"
#include "Socket.h"
#include "Channel.h"
#include "Buffer.h"
#include "ThreadPool.h"
#include "MysqlPool.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <string>
#include <sstream>
#include <cstdlib>
#include <ctime>

#ifdef USE_REDIS
#include "RedisPool.h"
#endif

Connection::Connection(EventLoop *loop, std::unique_ptr<Socket> sock, ThreadPool *dbPool)
    : loop_(loop), sock_(std::move(sock)), dbPool_(dbPool)
{
    channel_ = std::make_unique<Channel>(loop, sock_->getFd());
    channel_->enableReading();
    channel_->useET();
    std::function<void()> cb = std::bind(&Connection::handleMessage, this);
    channel_->setCallback(cb);
}

Connection::~Connection()
{
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

// ==================== HTTP Response ====================

std::string Connection::getResponse(const std::string &url)
{
    if (url == "/")
    {
        return "<html><body><h1>Welcome to My HTTP Server</h1>"
               "<p>This is a simple HTTP server built with epoll reactor pattern.</p>"
               "<p>Try: <a href=\"/hello\">/hello</a></p>"
               "<p>API: POST /register, POST /login</p>"
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

void Connection::sendHttpResponse(int fd, int statusCode, const std::string &contentType,
                                  const std::string &body)
{
    std::string statusText;
    switch (statusCode)
    {
    case 200:
        statusText = "OK";
        break;
    case 401:
        statusText = "Unauthorized";
        break;
    case 409:
        statusText = "Conflict";
        break;
    default:
        statusText = "Not Found";
        break;
    }

    std::string response =
        "HTTP/1.1 " + std::to_string(statusCode) + " " + statusText + "\r\n"
        "Content-Type: " + contentType + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n\r\n" + body;

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

// ==================== DB Handlers ====================

static std::string generateToken()
{
    static const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string token;
    token.reserve(32);
    for (int i = 0; i < 32; ++i)
    {
        token += charset[rand() % (sizeof(charset) - 1)];
    }
    return token;
}

void Connection::handleRegister(const std::string &request, int fd)
{
    std::string body = parseBody(request);
    std::string username = getJsonValue(body, "username");
    std::string password = getJsonValue(body, "password");

    if (username.empty() || password.empty())
    {
        sendHttpResponse(fd, 400, "application/json", "{\"error\":\"missing username or password\"}");
        return;
    }

    // Submit blocking DB work to the DB thread pool
    dbPool_->add([fd, username, password]()
    {
        MysqlGuard guard;
        MYSQL *conn = guard.get();

        // Escape input to prevent SQL injection
        char esc_user[129], esc_pass[257];
        mysql_real_escape_string(conn, esc_user, username.c_str(), username.size());
        mysql_real_escape_string(conn, esc_pass, password.c_str(), password.size());

        // Check if user already exists
        std::string query = "SELECT 1 FROM users WHERE username='" + std::string(esc_user) + "' LIMIT 1";
        if (mysql_query(conn, query.c_str()))
        {
            sendHttpResponse(fd, 500, "application/json", "{\"error\":\"database error\"}");
            return;
        }

        MYSQL_RES *res = mysql_store_result(conn);
        if (res && mysql_num_rows(res) > 0)
        {
            mysql_free_result(res);
            sendHttpResponse(fd, 409, "application/json", "{\"error\":\"user already exists\"}");
            return;
        }
        if (res)
            mysql_free_result(res);

        // Insert new user
        std::string insert = "INSERT INTO users(username, password) VALUES('" +
                             std::string(esc_user) + "','" + std::string(esc_pass) + "')";
        if (mysql_query(conn, insert.c_str()))
        {
            sendHttpResponse(fd, 500, "application/json", "{\"error\":\"database error\"}");
            return;
        }

        sendHttpResponse(fd, 200, "application/json", "{\"message\":\"register success\"}");
    });
}

void Connection::handleLogin(const std::string &request, int fd)
{
    std::string body = parseBody(request);
    std::string username = getJsonValue(body, "username");
    std::string password = getJsonValue(body, "password");

    if (username.empty() || password.empty())
    {
        sendHttpResponse(fd, 400, "application/json", "{\"error\":\"missing username or password\"}");
        return;
    }

    dbPool_->add([fd, username, password]()
    {
        std::string dbPassword;
        bool found = false;

#ifdef USE_REDIS
        // Try Redis cache first
        std::string cacheKey = "user:" + username;
        std::string cached = RedisPool::getInstance().get(cacheKey);
        if (!cached.empty())
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

            char esc_user[129];
            mysql_real_escape_string(conn, esc_user, username.c_str(), username.size());

            std::string query = "SELECT password FROM users WHERE username='" +
                                std::string(esc_user) + "' LIMIT 1";
            if (mysql_query(conn, query.c_str()))
            {
                sendHttpResponse(fd, 500, "application/json", "{\"error\":\"database error\"}");
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
                RedisPool::getInstance().setex(cacheKey, 300, dbPassword);
            }
#endif
        }

        if (!found)
        {
            sendHttpResponse(fd, 401, "application/json", "{\"error\":\"user not found\"}");
            return;
        }

        if (password != dbPassword)
        {
            sendHttpResponse(fd, 401, "application/json", "{\"error\":\"wrong password\"}");
            return;
        }

        // Login success, generate session token
        std::string token = generateToken();

#ifdef USE_REDIS
        // Store session in Redis (TTL 3600s = 1 hour)
        RedisPool::getInstance().setex("session:" + token, 3600, username);
#endif

        sendHttpResponse(fd, 200, "application/json",
                         "{\"message\":\"login success\",\"token\":\"" + token + "\"}");
    });
}

// ==================== Main Handler ====================

void Connection::handleMessage()
{
    int sockfd = sock_->getFd();
    Buffer readBuffer;
    char buf[1024];
    while (true)
    {
        bzero(&buf, sizeof(buf));
        ssize_t read_bytes = read(sockfd, buf, sizeof(buf));
        if (read_bytes > 0)
        {
            readBuffer.append(buf, read_bytes);
        }
        else if (read_bytes == -1 && errno == EINTR)
        {
            continue;
        }
        else if (read_bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            if (readBuffer.size() == 0)
            {
                break;
            }

            std::string request(readBuffer.c_str());
            std::string method = parseMethod(request);
            std::string url = parseUrl(request);

            // POST routes (handled async via DB thread pool)
            if (method == "POST" && url == "/register")
            {
                handleRegister(request, sockfd);
                readBuffer.clear();
                break;
            }
            if (method == "POST" && url == "/login")
            {
                handleLogin(request, sockfd);
                readBuffer.clear();
                break;
            }

            // GET routes (handled inline)
            std::string body = getResponse(url);
            int statusCode = (url == "/" || url == "/hello") ? 200 : 404;
            sendHttpResponse(sockfd, statusCode, "text/html", body);

            readBuffer.clear();
            break;
        }
        else if (read_bytes == 0)
        {
            printf("EOF! client fd %d disconnected\n", sockfd);
            deleteConnectionCallback_(sock_->getFd());
            break;
        }
        else
        {
            printf("Connection reset by peer\n");
            deleteConnectionCallback_(sock_->getFd());
            break;
        }
    }
}

void Connection::setDeleteConnectionCallback(std::function<void(int)> cb)
{
    deleteConnectionCallback_ = cb;
}
