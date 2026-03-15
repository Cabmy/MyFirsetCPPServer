#pragma once
#include <functional>

class EventLoop;
class Socket;
class InetAddress;
class Channel;

class Acceptor
{
private:
    EventLoop *loop_;
    Socket *sock_;
    InetAddress *addr_;
    Channel *acceptChannel_;

public:
    Acceptor(EventLoop *loop);
    ~Acceptor();

    void acceptConnection();
    std::function<void(Socket *)> newConnectionCallback;
    void setNewConnectionCallback(std::function<void(Socket *)>);
};