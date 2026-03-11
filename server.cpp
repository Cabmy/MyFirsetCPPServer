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

// void set_non_blocking(int fd)
// {
//     int flags = fcntl(fd, F_GETFL);
//     fcntl(fd, F_SETFL, flags | O_NONBLOCK);
// }

void handleReadEv(int fd);

int main()
{
    // int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    // errif(sockfd == -1, "socket creat error");
    Socket *serv_sock = new Socket();

    // struct sockaddr_in serv_addr; // IPv4专用数据结构，in指internet
    // bzero(&serv_addr, sizeof(serv_addr));

    // serv_addr.sin_family = AF_INET;
    // serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // 把点分十进制转为32位网络字节序
    // serv_addr.sin_port = htons(8888);                   // host to network short，避免大小端字节序问题

    InetAddress *serv_addr = new InetAddress("127.0.0.1", 8888);

    // 转为sockaddr通用类型，支持Ipv6等
    // errif(bind(sockfd, (sockaddr *)&serv_addr->getAddr(), sizeof(serv_addr->getAddrLen())) == -1, "socket bind error");
    serv_sock->bind(serv_addr);

    // errif(listen(sockfd, SOMAXCONN) == -1, "socket listen error");
    serv_sock->listen();

    // int epfd = epoll_create1(0);
    // errif(epfd == -1, "epoll create error");

    // struct epoll_event events[MAX_EVENTS], ev;
    // bzero(&events, sizeof(events));
    // bzero(&ev, sizeof(ev));
    Epoll *ep = new Epoll();

    // // 将服务器socket添加到epoll
    // ev.data.fd = sockfd;
    // ev.events = EPOLLIN | EPOLLET;
    // set_non_blocking(sockfd); // 需要转为非阻塞fd
    // epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);
    serv_sock->setnonblocking();
    ep->addFd(serv_sock->getFd(), EPOLLIN | EPOLLET);

    while (true)
    {
        // int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        // errif(nfds == -1, "epoll wait error");
        std::vector<epoll_event> events = ep->activeEvents();

        for (epoll_event ev : events)
        {
            if (ev.data.fd == serv_sock->getFd())
            {
                // 新客户端连接
                // struct sockaddr_in clnt_addr;
                // socklen_t clnt_addr_len = sizeof(clnt_addr);
                // bzero(&clnt_addr, clnt_addr_len);

                // int fd = accept(sockfd, (sockaddr *)&clnt_addr, &clnt_addr_len);
                // errif(fd == -1, "socket accept error\n");

                /*会发生内存泄露*/
                InetAddress *clnt_addr = new InetAddress();
                Socket *clnt_sock = new Socket(serv_sock->accept(clnt_addr));

                // 打印客户端信息
                // inet_ntoa转化成可读字符串； network to host short
                printf("new client fd %d! IP: %s Port: %d\n", clnt_sock->getFd(), inet_ntoa(clnt_addr->getAddr().sin_addr), ntohs(clnt_addr->getAddr().sin_port));

                // 将新客户端socket添加到epoll
                // bzero(&ev, sizeof(ev));
                // ev.data.fd = fd;
                // ev.events = EPOLLIN | EPOLLET;
                // set_non_blocking(fd);
                // epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
                clnt_sock->setnonblocking();
                ep->addFd(clnt_sock->getFd(), EPOLLIN | EPOLLET);
            }
            else if (ev.events & EPOLLIN)
            {
                // // 可读事件
                // int fd = events[i].data.fd;
                // char buf[MAX_BUFFER];
                // while (true)
                // {
                //     // 一次读取buf大小数据，直到全部读完
                //     bzero(&buf, sizeof(buf));
                //     ssize_t read_bytes = read(fd, buf, sizeof(buf));
                //     if (read_bytes > 0)
                //     {
                //         printf("message from client fd %d: %s\n", fd, buf);
                //         write(fd, buf, sizeof(buf)); // 将相同的数据写回客户端, 暂时不考虑错误处理
                //     }
                //     else if (read_bytes == -1 && errno == EINTR)
                //     {
                //         // 读取过程中被信号打断，继续读取
                //         printf("continue reading\n");
                //         break;
                //     }
                //     else if (read_bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
                //     {
                //         // 已经读完
                //         printf("finish reading once, errno: %d\n", errno);
                //         break;
                //     }
                //     else if (read_bytes == 0)
                //     {
                //         // 客户端断开连接
                //         printf("EOF! client fd %d disconnected\n", fd);
                //         close(fd); // 关闭socket会自动将文件描述符从epoll树上移除
                //         break;
                //     }
                // }
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