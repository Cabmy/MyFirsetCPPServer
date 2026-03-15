#include "Acceptor.h"
#include "Socket.h"
#include "Channel.h"
#include "InetAddress.h"
#include <stdio.h>

Acceptor::Acceptor(EventLoop *loop) : loop_(loop)
{
    sock_ = new Socket();
    InetAddress *addr = new InetAddress("127.0.0.1", 8888);
    sock_->bind(addr);
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
    delete acceptChannel_;
}

void Acceptor::acceptConnection()
{
    InetAddress *clnt_addr = new InetAddress();
    Socket *clnt_sock = new Socket(sock_->accept(clnt_addr));
    printf("new client fd %d! IP: %s Port: %d\n", clnt_sock->getFd(), inet_ntoa(clnt_addr->getAddr().sin_addr), ntohs(clnt_addr->getAddr().sin_port));
    clnt_sock->setnonblocking();
    newConnectionCallback_(clnt_sock);
    delete clnt_addr;
}

void Acceptor::setNewConnectionCallback(std::function<void(Socket *)> cb)
{
    newConnectionCallback_ = cb;
}