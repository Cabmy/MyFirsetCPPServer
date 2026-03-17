#pragma once
#include <functional>

class EventLoop;
class Socket;
class Channel;
class Buffer;

class Connection
{
private:
    EventLoop *loop_;
    Socket *sock_;
    Channel *channel_;
    std::function<void(Socket *)> deleteConnectionCallback_;
    Buffer *readBuffer;

public:
    Connection(EventLoop *loop, Socket *sock);
    ~Connection();

    void echo(int sockfd);
    void setDeleteConnectionCallback(std::function<void(Socket *)>);
};