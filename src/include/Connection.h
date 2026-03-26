#pragma once
#include <functional>
#include <memory>
#include <string>

class EventLoop;
class Socket;
class Channel;
class Buffer;
class ThreadPool;

class Connection
{
private:
    EventLoop *loop_;
    std::unique_ptr<Socket> sock_;
    std::unique_ptr<Channel> channel_;
    std::function<void(int)> deleteConnectionCallback_;
    ThreadPool *dbPool_;

    std::string parseMethod(const std::string &request);
    std::string parseUrl(const std::string &request);
    std::string parseBody(const std::string &request);
    std::string getResponse(const std::string &url);

    void handleRegister(const std::string &request, int fd);
    void handleLogin(const std::string &request, int fd);

    static std::string getJsonValue(const std::string &json, const std::string &key);
    static void sendHttpResponse(int fd, int statusCode, const std::string &contentType,
                                 const std::string &body);

public:
    Connection(EventLoop *loop, std::unique_ptr<Socket> sock, ThreadPool *dbPool);
    ~Connection();

    void handleMessage();
    void setDeleteConnectionCallback(std::function<void(int)>);
};
