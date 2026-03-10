#include "Socket.h"
#include "InetAddress.h"
#include "util.h"
#include <unistd.h>

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

void Socket::Sbind(InetAddress *addr)
{
    errif(bind(fd_, (sockaddr *)&addr->getAddr(), addr->getAddrLen()) == -1, "socket bind error");
}

void Socket::Slisten()
{
}
void Socket::Ssetnonblocking()
{
}

int Socket::Saccept(InetAddress *addr)
{
}

int Socket::getFd()
{
}