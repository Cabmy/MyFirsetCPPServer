#include "Server.h"
#include "Socket.h"
#include "Channel.h"
#include "InetAddress.h"
#include "Acceptor.h"
#include "Connection.h"
#include "ThreadPool.h"
#include "EventLoop.h"
#include "Config.h"
#include <functional>
#include <thread>
#include <ctime>
#include <sys/socket.h>

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
        thPool_->add(std::bind(&EventLoop::loop, subReactors_.back().get()));
    }

    // DB thread pool for blocking MySQL/Redis operations
    dbPool_ = std::make_unique<ThreadPool>(config::kDbThreads);
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
    int idx = fd % subReactors_.size();
    // Pass the delete callback into the ctor so it is set before the channel
    // is registered to epoll (avoids the early-event null-callback race).
    std::function<void(int)> cb = std::bind(&Server::deleteConnection, this, std::placeholders::_1);
    auto conn = std::make_unique<Connection>(subReactors_[idx].get(), std::move(sock), dbPool_.get(), cb);
    {
        std::lock_guard<std::mutex> lock(connMtx_);
        connections_[fd] = std::move(conn);
    }
}

void Server::stop()
{
    // Set quit on every reactor; each loop observes it within one poll timeout.
    mainReactor_->quit();
    for (auto &r : subReactors_)
        r->quit();
}

void Server::sweepIdle()
{
    int timeout = config::kIdleTimeoutSec;
    if (timeout <= 0)
        return;
    long now = (long)time(nullptr);
    std::lock_guard<std::mutex> lock(connMtx_);
    for (auto &kv : connections_)
    {
        if (now - kv.second->lastActiveSec() > timeout)
        {
            // Don't delete here (wrong thread): half-close so the owning
            // sub-reactor sees EOF and runs deleteConnection safely.
            ::shutdown(kv.first, SHUT_RDWR);
        }
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