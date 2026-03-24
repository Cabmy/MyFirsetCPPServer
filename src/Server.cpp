#include "Server.h"
#include "Socket.h"
#include "Channel.h"
#include "InetAddress.h"
#include "Acceptor.h"
#include "Connection.h"
#include "ThreadPool.h"
#include "EventLoop.h"
#include <functional>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <thread>

Server::Server(EventLoop *lp) : mainReactor_(lp), acceptor_(nullptr)
{
    acceptor_ = std::make_unique<Acceptor>(mainReactor_);
    std::function<void(std::unique_ptr<Socket>)> cb = std::bind(&Server::newConnection, this, std::placeholders::_1);
    acceptor_->setNewConnectionCallback(cb);

    int size = std::thread::hardware_concurrency();
    thPool_ = std::make_unique<ThreadPool>(size);
    for (int i = 0; i < size; ++i)
    {
        subReactors_.push_back(std::make_unique<EventLoop>());
    }

    for (int i = 0; i < size; ++i)
    {
        std::function<void()> sub_loop = std::bind(&EventLoop::loop, subReactors_[i].get());
        thPool_->add(sub_loop);
    }
}

Server::~Server()
{
}

void Server::newConnection(std::unique_ptr<Socket> sock)
{
    if (sock->getFd() == -1)
    {
        return;
    }
    int fd = sock->getFd();
    int random = sock->getFd() % subReactors_.size();
    auto conn = std::make_unique<Connection>(subReactors_[random].get(), std::move(sock));
    std::function<void(int)> cb = std::bind(&Server::deleteConnection, this, std::placeholders::_1);
    conn->setDeleteConnectionCallback(cb);
    {
        std::lock_guard<std::mutex> lock(connMtx_);
        connections_[fd] = std::move(conn);
    }
}

void Server::deleteConnection(int fd)
{
    if (fd == -1)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(connMtx_);
    connections_.erase(fd);
}