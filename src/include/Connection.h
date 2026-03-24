#pragma once
#include <functional>
#include <memory>
#include <string>

class EventLoop;
class Socket;
class Channel;
class Buffer;

class Connection
{
private:
    EventLoop *loop_;
    std::unique_ptr<Socket> sock_;
    std::unique_ptr<Channel> channel_;
    std::function<void(int)> deleteConnectionCallback_;
    // Buffer *readBuffer_;

    std::string parseUrl(const std::string &request);
    std::string getResponse(const std::string &url);

public:
    Connection(EventLoop *loop, std::unique_ptr<Socket> sock);
    ~Connection();

    // void echo();
    void handleMessage();
    void setDeleteConnectionCallback(std::function<void(int)>);
    void send();
};