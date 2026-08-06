#pragma once
#include <functional>
#include <memory>
#include <string>
#include <atomic>
#include <sys/types.h>

class EventLoop;
class Socket;
class Channel;
class ThreadPool;

class Connection
{
private:
    EventLoop *loop_;
    std::unique_ptr<Socket> sock_;
    std::unique_ptr<Channel> channel_;
    std::function<void(int)> deleteConnectionCallback_;
    ThreadPool *dbPool_;

    // Output buffer for reactor-thread responses: if a write hits EAGAIN the
    // remainder stays here and EPOLLOUT drives handleWrite() to finish it.
    std::string writeBuf_;
    size_t writeIndex_ = 0;

    // Persistent input buffer across events, so a keep-alive connection can
    // accumulate and frame successive requests (HTTP/1.1 persistent connection).
    std::string inputBuffer_;
    // When a non-keep-alive response finishes draining, close the connection.
    bool closeAfterWrite_ = false;

    // Last activity time (epoch seconds), read by the idle-sweep from another
    // thread -> atomic. Updated on every read event.
    std::atomic<long> lastActiveSec_{0};

    // Pending zero-copy file body (sendfile) after the header buffer drains.
    int fileFd_ = -1;
    off_t fileOffset_ = 0;
    size_t fileRemaining_ = 0;

    std::string parseMethod(const std::string &request);
    std::string parseUrl(const std::string &request);
    std::string parseBody(const std::string &request);
    std::string getResponse(const std::string &url);

    // Frame and dispatch every complete request currently in inputBuffer_.
    void processRequests(int fd);

    void handleRegister(const std::string &request, int fd, bool keepAlive);
    void handleLogin(const std::string &request, int fd, bool keepAlive);
    void handleProfile(const std::string &request, int fd, bool keepAlive);
    void handleLogout(const std::string &request, int fd, bool keepAlive);

    // Reactor-thread buffered send: queues response and drains it, registering
    // EPOLLOUT on partial write. Safe only on the owning sub-reactor thread.
    // keepAlive=false closes the connection once the buffer is fully sent.
    void sendResponse(int statusCode, const std::string &contentType, const std::string &body,
                      bool keepAlive);
    // Zero-copy static file send (headers via buffer, body via sendfile()).
    // Returns false if the file cannot be opened (caller falls back to inline).
    bool sendFile(int statusCode, const std::string &contentType, const std::string &path,
                  bool keepAlive);
    void writeFromBuffer();
    void handleWrite();

    static std::string getJsonValue(const std::string &json, const std::string &key);
    static std::string parseBearerToken(const std::string &request);
    static std::string buildHttpResponse(int statusCode, const std::string &contentType,
                                         const std::string &body, bool keepAlive);
    // Direct best-effort write, used by DB worker threads for small responses.
    static void sendHttpResponse(int fd, int statusCode, const std::string &contentType,
                                 const std::string &body, bool keepAlive);
    // JSON shorthand over sendHttpResponse (all DB-thread responses are JSON).
    static void sendJson(int fd, int statusCode, const std::string &body, bool keepAlive);
    // Shared /register + /login input validation; sends the 400 itself.
    static bool checkCredentials(int fd, bool keepAlive, const std::string &username,
                                 const std::string &password);
    // Run a DB-pool task body with uniform exception handling (logged 500 on throw).
    static void runDbTask(const char *tag, int fd, bool keepAlive,
                          const std::function<void()> &work);

public:
    Connection(EventLoop *loop, std::unique_ptr<Socket> sock, ThreadPool *dbPool,
               std::function<void(int)> deleteCb);
    ~Connection();

    void handleMessage();
    long lastActiveSec() const { return lastActiveSec_.load(std::memory_order_relaxed); }
};
