#include "Server.h"
#include "Socket.h"
#include "Channel.h"
#include "InetAddress.h"
#include "Acceptor.h"
#include "Connection.h"
#include <functional>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

Server::Server(EventLoop *lp) : loop_(lp), acceptor_(nullptr)
{
    acceptor_ = new Acceptor(loop_);
    std::function<void(Socket *)> cb = std::bind(&Server::newConnection, this, std::placeholders::_1);
    acceptor_->setNewConnectionCallback(cb);
}

Server::~Server()
{
    delete acceptor_;
}

void Server::newConnection(Socket *sock)
{
    if (sock->getFd() == -1)
    {
        return;
    }

    Connection *conn = new Connection(loop_, sock);
    std::function<void(Socket *)> cb = std::bind(&Server::deleteConnection, this, std::placeholders::_1);
    conn->setDeleteConnectionCallback(cb);
    connections_[sock->getFd()] = conn;
}

void Server::deleteConnection(Socket *sock)
{
    int fd = sock->getFd();
    if (fd == -1)
    {
        return;
    }

    auto it = connections_.find(fd);
    if (it != connections_.end())
    {
        Connection *conn = connections_[fd];
        connections_.erase(fd);
        delete conn;
    }
}