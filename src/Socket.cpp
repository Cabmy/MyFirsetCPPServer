#include "Socket.h"
#include "InetAddress.h"
#include "util.h"
#include <unistd.h>
#include <fcntl.h>

Socket::Socket() : fd_(-1)
{
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    errif(fd_ == -1, "socket create error");
}

Socket::Socket(int fd) : fd_(fd)
{
    errif(fd_ == -1, "socket create error");
}

Socket::~Socket()
{
    if (fd_ != -1)
    {
        close(fd_);
        fd_ = -1;
    }
}

void Socket::bind(InetAddress *addr)
{
    errif(::bind(fd_, (sockaddr *)&addr->getAddr(), addr->getAddrLen()) == -1, "socket bind error");
}

void Socket::listen()
{
    int backlog = 65535;
    errif(::listen(fd_, backlog) == -1, "socket listen error");
}
void Socket::setnonblocking()
{
    int flags = fcntl(fd_, F_GETFL);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
}

int Socket::accept(InetAddress *addr)
{
    int clnt_sockfd = ::accept(fd_, (sockaddr *)&addr->getAddr(), &addr->getAddrLen());
    errif(clnt_sockfd == -1, "socket accept error\n");
    return clnt_sockfd;
}

int Socket::getFd()
{
    return fd_;
}

void Socket::connect(InetAddress *addr)
{
    errif(::connect(fd_, (sockaddr *)&addr->getAddr(), addr->getAddrLen()) == -1, "socket connect error");
}