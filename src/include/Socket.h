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
    void setNoDelay(); // 关闭 Nagle (TCP_NODELAY)，避免 write 头+sendfile 体的 40ms 延迟ACK停顿

    int accept(InetAddress *addr);
    // 客户端连接使用接口
    void connect(InetAddress *addr);

    int getFd();
};