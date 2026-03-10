#pragma once
#include "InetAddress.h"

class Socket
{
private:
    int fd_;

public:
    Socket();
    Socket(int fd);
    ~Socket();

    void Sbind(InetAddress *addr);
    void Slisten();
    void Ssetnonblocking();

    int Saccept(InetAddress *addr);

    int getFd();
};