#pragma once
#include <functional>
#include <memory>

class EventLoop;
class Socket;
class Channel;

class Acceptor
{
private:
    EventLoop *loop_;
    std::unique_ptr<Socket> sock_;
    std::unique_ptr<Channel> acceptChannel_;
    std::function<void(std::unique_ptr<Socket>)> newConnectionCallback_;

public:
    Acceptor(EventLoop *loop);
    ~Acceptor();

    void acceptConnection();
    void setNewConnectionCallback(std::function<void(std::unique_ptr<Socket>)>);
};