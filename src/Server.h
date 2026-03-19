#pragma once
#include <map>

class EventLoop;
class Socket;
class Acceptor;
class Connection;

class Server
{
private:
    EventLoop *loop_;
    Acceptor *acceptor_;
    std::map<int, Connection *> connections_;

public:
    Server(EventLoop *lp);
    ~Server();

    void newConnection(Socket *serv_sock);
    void deleteConnection(Socket *sock);
};