#pragma once
#include <arpa/inet.h>

class InetAddress
{
private:
    struct sockaddr_in addr_;
    socklen_t addr_len_;

public:
    InetAddress();
    InetAddress(const char *ip, uint16_t port);
    ~InetAddress();

    sockaddr_in getAddr();
    socklen_t getAddrLen();
};