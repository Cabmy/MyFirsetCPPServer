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

    void bind(InetAddress *addr);
    void listen();
    void setnonblocking();

    int accept(InetAddress *addr);
    // 客户端连接使用接口
    void connect(InetAddress *addr);

    int getFd();
};