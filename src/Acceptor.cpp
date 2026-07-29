#include "Acceptor.h"
#include "Socket.h"
#include "Channel.h"
#include "InetAddress.h"
#include "Config.h"
#include <stdio.h>
#include <errno.h>

Acceptor::Acceptor(EventLoop *loop) : loop_(loop)
{
    sock_ = std::make_unique<Socket>();
    InetAddress addr(config::serverHost().c_str(), config::serverPort());
    sock_->bind(&addr);
    sock_->listen();
    sock_->setnonblocking();
    acceptChannel_ = std::make_unique<Channel>(loop_, sock_->getFd()); // 监听sock对应Channel
    std::function<void()> cb = std::bind(&Acceptor::acceptConnection, this);
    acceptChannel_->setCallback(cb);
    acceptChannel_->enableReading();
}

Acceptor::~Acceptor()
{
}

void Acceptor::acceptConnection()
{
    while (true) {
        InetAddress clnt_addr;
        int clnt_fd = sock_->accept(&clnt_addr);
        if (clnt_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            perror("accept error");
            break;
        }
        auto clnt_sock = std::make_unique<Socket>(clnt_fd);
        clnt_sock->setnonblocking();
        clnt_sock->setNoDelay();
        newConnectionCallback_(std::move(clnt_sock));
    }
}

void Acceptor::setNewConnectionCallback(std::function<void(std::unique_ptr<Socket>)> cb)
{
    newConnectionCallback_ = cb;
}