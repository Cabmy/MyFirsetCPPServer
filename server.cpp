#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include "util.h"

int main()
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    errif(sockfd == -1, "socket creat error\n");

    struct sockaddr_in serv_addr; // IPv4专用数据结构，in指internet
    bzero(&serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // 把点分十进制转为32位网络字节序
    serv_addr.sin_port = htons(8888);                   // host to network short，避免大小端字节序问题

    // 转为sockaddr通用类型，支持Ipv6等
    errif(bind(sockfd, (sockaddr *)&serv_addr, sizeof(serv_addr)) == -1, "socket bind error\n");

    errif(listen(sockfd, SOMAXCONN) == -1, "socket listen error\n");

    struct sockaddr_in clnt_addr;
    socklen_t clnt_addr_len = sizeof(clnt_addr);
    bzero(&clnt_addr, clnt_addr_len);

    int clnt_sockfd = accept(sockfd, (sockaddr *)&clnt_addr, &clnt_addr_len);
    errif(clnt_sockfd == -1, "socket accept error\n");

    // 打印客户端信息
    // inet_ntoa转化成可读字符串； network to host short
    printf("new client fd %d! IP: %s Port: %d\n", clnt_sockfd, inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port));

    while (true)
    {
        char buf[1024];
        bzero(&buf, sizeof(buf));
        ssize_t read_bytes = read(clnt_sockfd, buf, sizeof(buf));
        if (read_bytes > 0)
        {
            printf("message from client fd %d: %s\n", clnt_sockfd, buf);
            write(clnt_sockfd, buf, sizeof(buf)); // 将相同的数据写回客户端, 暂时不考虑错误处理
        }
        else if (read_bytes == 0)
        {
            printf("client fd %d disconnected\n", clnt_sockfd);
            close(clnt_sockfd);
            break;
        }
        else
        {
            // 返回-1,发生错误
            close(clnt_sockfd);
            errif(true, "socket read error\n");
        }
    }

    return 0;
}