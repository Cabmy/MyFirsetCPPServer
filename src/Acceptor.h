#pragma once
#include <functional>

class EventLoop;
class Socket;
class Channel;

class Acceptor
{
private:
    EventLoop *loop_;
    Socket *sock_;
    Channel *acceptChannel_;
    std::function<void(Socket *)> newConnectionCallback_;

public:
    Acceptor(EventLoop *loop);
    ~Acceptor();

    void acceptConnection();
    void setNewConnectionCallback(std::function<void(Socket *)>);
};