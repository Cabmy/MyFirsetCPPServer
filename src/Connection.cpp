#include "Connection.h"
#include "Socket.h"
#include "Channel.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define MAX_BUFFER 1024

Connection::Connection(EventLoop *loop, Socket *sock) : loop_(loop), sock_(sock)
{
    channel_ = new Channel(loop, sock->getFd());
    std::function<void()> cb = std::bind(&Connection::echo, this, sock->getFd());
    channel_->setCallback(cb);
    channel_->enableReading();
}

Connection::~Connection()
{
    delete channel_;
    delete sock_;
}

void Connection::echo(int sockfd)
{
    char buf[MAX_BUFFER];
    while (true)
    {
        // 一次读取buf大小数据，直到全部读完
        bzero(&buf, sizeof(buf));
        ssize_t read_bytes = read(sockfd, buf, sizeof(buf));
        if (read_bytes > 0)
        {
            printf("message from client fd %d: %s\n", sockfd, buf);
            write(sockfd, buf, sizeof(buf)); // 将相同的数据写回客户端, 暂时不考虑错误处理
        }
        else if (read_bytes == -1 && errno == EINTR)
        {
            // 读取过程中被信号打断，继续读取
            printf("continue reading\n");
            continue;
        }
        else if (read_bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            // 已经读完
            printf("finish reading once, errno: %d\n", errno);
            break;
        }
        else if (read_bytes == 0)
        {
            // 客户端断开连接
            printf("EOF! client fd %d disconnected\n", sockfd);
            // close(sockfd); // 关闭socket会自动将文件描述符从epoll树上移除
            deleteConnectionCallback_(sock_);
            break;
        }
    }
}

void Connection::setDeleteConnectionCallback(std::function<void(Socket *)> cb)
{
    deleteConnectionCallback_ = cb;
}