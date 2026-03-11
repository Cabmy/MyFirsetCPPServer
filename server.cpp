#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <vector>
#include "util.h"
#include "InetAddress.h"
#include "Socket.h"
#include "Epoll.h"

#define MAX_EVENTS 1024
#define MAX_BUFFER 1024

void handleReadEv(int fd);

int main()
{
    Socket *serv_sock = new Socket();
    InetAddress *serv_addr = new InetAddress("127.0.0.1", 8888);

    serv_sock->bind(serv_addr);
    serv_sock->listen();

    Epoll *ep = new Epoll();

    // 将服务器socket添加到epoll
    serv_sock->setnonblocking();
    ep->addFd(serv_sock->getFd(), EPOLLIN | EPOLLET);

    while (true)
    {
        std::vector<epoll_event> events = ep->activeEvents();

        for (epoll_event ev : events)
        {
            if (ev.data.fd == serv_sock->getFd())
            {
                // 新客户端连接
                /*会发生内存泄露*/
                InetAddress *clnt_addr = new InetAddress();
                Socket *clnt_sock = new Socket(serv_sock->accept(clnt_addr));

                // 打印客户端信息
                // inet_ntoa转化成可读字符串； network to host short
                printf("new client fd %d! IP: %s Port: %d\n", clnt_sock->getFd(), inet_ntoa(clnt_addr->getAddr().sin_addr), ntohs(clnt_addr->getAddr().sin_port));

                // 将新客户端socket添加到epoll
                clnt_sock->setnonblocking();
                ep->addFd(clnt_sock->getFd(), EPOLLIN | EPOLLET);
            }
            else if (ev.events & EPOLLIN)
            {
                // 可读事件
                handleReadEv(ev.data.fd);
            }
            else
            {
                // 其他事件
                printf("something else happened\n");
            }
        }
    }

    delete serv_sock;
    delete serv_addr;

    return 0;
}

void handleReadEv(int fd)
{
    char buf[MAX_BUFFER];
    while (true)
    {
        // 一次读取buf大小数据，直到全部读完
        bzero(&buf, sizeof(buf));
        ssize_t read_bytes = read(fd, buf, sizeof(buf));
        if (read_bytes > 0)
        {
            printf("message from client fd %d: %s\n", fd, buf);
            write(fd, buf, sizeof(buf)); // 将相同的数据写回客户端, 暂时不考虑错误处理
        }
        else if (read_bytes == -1 && errno == EINTR)
        {
            // 读取过程中被信号打断，继续读取
            printf("continue reading\n");
            break;
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
            printf("EOF! client fd %d disconnected\n", fd);
            close(fd); // 关闭socket会自动将文件描述符从epoll树上移除
            break;
        }
    }
}