#pragma once

class EventLoop;
class Socket;
class Acceptor;

class Server
{
private:
    EventLoop *loop_;
    Acceptor *acceptor_;

public:
    Server(EventLoop *lp);
    ~Server();

    void handleReadEvent(int fd);
    void newConnection(Socket *serv_sock);
};