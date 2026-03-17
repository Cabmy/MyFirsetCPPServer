#include "Connection.h"
#include "Socket.h"
#include "Channel.h"
#include "Buffer.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

Connection::Connection(EventLoop *loop, Socket *sock) : loop_(loop), sock_(sock)
{
    channel_ = new Channel(loop, sock->getFd());
    readBuffer = new Buffer();
    std::function<void()> cb = std::bind(&Connection::echo, this, sock->getFd());
    channel_->setCallback(cb);
    channel_->enableReading();
}

Connection::~Connection()
{
    delete readBuffer;
    delete channel_;
    delete sock_;
}

void Connection::echo(int sockfd)
{
    char buf[1024];
    while (true)
    {
        // 一次读取buf大小数据，直到全部读完
        bzero(&buf, sizeof(buf));
        ssize_t read_bytes = read(sockfd, buf, sizeof(buf));
        if (read_bytes > 0)
        {
            readBuffer->append(buf, read_bytes);
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
            printf("message from client fd %d: %s\n", sockfd, readBuffer->c_str());
            errif(write(sockfd, readBuffer->c_str(), readBuffer->size()) == -1, "socket write error");
            readBuffer->clear();
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