#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include "util.h"

#define MAX_EVENTS 1024
#define MAX_BUFFER 1024

void set_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main()
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    errif(sockfd == -1, "socket creat error");

    struct sockaddr_in serv_addr; // IPv4专用数据结构，in指internet
    bzero(&serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // 把点分十进制转为32位网络字节序
    serv_addr.sin_port = htons(8888);                   // host to network short，避免大小端字节序问题

    // 转为sockaddr通用类型，支持Ipv6等
    errif(bind(sockfd, (sockaddr *)&serv_addr, sizeof(serv_addr)) == -1, "socket bind error");

    errif(listen(sockfd, SOMAXCONN) == -1, "socket listen error");

    int epfd = epoll_create1(0);
    errif(epfd == -1, "epoll create error");

    struct epoll_event events[MAX_EVENTS], ev;
    bzero(&events, sizeof(events));
    bzero(&ev, sizeof(ev));

    // 将服务器socket添加到epoll
    ev.data.fd = sockfd;
    ev.events = EPOLLIN | EPOLLET;
    set_non_blocking(sockfd); // 需要转为非阻塞fd
    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);

    while (true)
    {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        errif(nfds == -1, "epoll wait error");

        for (int i = 0; i < nfds; ++i)
        {
            if (events[i].data.fd == sockfd)
            {
                // 新客户端连接
                struct sockaddr_in clnt_addr;
                socklen_t clnt_addr_len = sizeof(clnt_addr);
                bzero(&clnt_addr, clnt_addr_len);

                int clnt_sockfd = accept(sockfd, (sockaddr *)&clnt_addr, &clnt_addr_len);
                errif(clnt_sockfd == -1, "socket accept error\n");

                // 打印客户端信息
                // inet_ntoa转化成可读字符串； network to host short
                printf("new client fd %d! IP: %s Port: %d\n", clnt_sockfd, inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port));

                // 将新客户端socket添加到epoll
                bzero(&ev, sizeof(ev));
                ev.data.fd = clnt_sockfd;
                ev.events = EPOLLIN | EPOLLET;
                set_non_blocking(clnt_sockfd);
                epoll_ctl(epfd, EPOLL_CTL_ADD, clnt_sockfd, &ev);
            }
            else if (events[i].events & EPOLLIN)
            {
                // 可读事件
                int clnt_sockfd = events[i].data.fd;
                char buf[MAX_BUFFER];
                while (true)
                {
                    // 一次读取buf大小数据，直到全部读完
                    bzero(&buf, sizeof(buf));
                    ssize_t read_bytes = read(clnt_sockfd, buf, sizeof(buf));
                    if (read_bytes > 0)
                    {
                        printf("message from client fd %d: %s\n", clnt_sockfd, buf);
                        write(clnt_sockfd, buf, sizeof(buf)); // 将相同的数据写回客户端, 暂时不考虑错误处理
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
                        printf("EOF! client fd %d disconnected\n", clnt_sockfd);
                        close(clnt_sockfd); // 关闭socket会自动将文件描述符从epoll树上移除
                        break;
                    }
                }
            }
            else
            {
                // 其他事件
                printf("something else happened\n");
            }
        }
    }

    close(sockfd);

    return 0;
}