#include "InetAddress.h"
#include <string.h>

InetAddress::InetAddress() : addr_len_(sizeof(addr_))
{
    bzero(&addr_, sizeof(addr_));
}

InetAddress::InetAddress(const char *ip, uint16_t port) : addr_len_(sizeof(addr_))
{
    bzero(&addr_, sizeof(addr_));

    addr_.sin_family = AF_INET;
    addr_.sin_addr.s_addr = inet_addr(ip);
    addr_.sin_port = htons(port);
}

InetAddress::~InetAddress()
{
}

sockaddr_in &InetAddress::getAddr()
{
    return addr_;
}

socklen_t &InetAddress::getAddrLen()
{
    return addr_len_;
}