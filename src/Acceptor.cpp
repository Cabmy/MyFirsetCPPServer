#include "Acceptor.h"
#include "Socket.h"
#include "InetAddress.h"
#include "Channel.h"
#include "Server.h"

Acceptor::Acceptor(EventLoop *loop) : loop_(loop)
{
    sock_ = new Socket();
    addr_ = new InetAddress("127.0.0.1", 8888);
    sock_->bind(addr_);
    sock_->listen();
    sock_->setnonblocking();
    acceptChannel_ = new Channel(loop_, sock_->getFd()); // 监听sock对应Channel
    std::function<void()> cb = std::bind(&Acceptor::acceptConnection, this);
    acceptChannel_->setCallback(cb);
    acceptChannel_->enableReading();
}

Acceptor::~Acceptor()
{
    delete sock_;
    delete addr_;
    delete acceptChannel_;
}

void Acceptor::acceptConnection()
{
    newConnectionCallback(sock_);
}

void Acceptor::setNewConnectionCallback(std::function<void(Socket *)> cb)
{
    newConnectionCallback = cb;
}