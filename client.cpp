#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "util.h"

#define MAX_BUFFER 1024

int main()
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in serv_addv;
    bzero(&serv_addv, sizeof(serv_addv));
    serv_addv.sin_family = AF_INET;
    serv_addv.sin_addr.s_addr = inet_addr("127.0.0.1");
    serv_addv.sin_port = htons(8888);

    errif(connect(sockfd, (sockaddr *)&serv_addv, sizeof(serv_addv)) == -1, "socket connect error");

    while (true)
    {
        char buf[MAX_BUFFER];
        bzero(&buf, sizeof(buf));
        scanf("%s", buf);
        ssize_t write_bytes = write(sockfd, buf, sizeof(buf));
        if (write_bytes == -1)
        {
            errif(true, "server socket disconnect");
            break;
        }

        // 读取从服务器写的值
        bzero(&buf, sizeof(buf));
        ssize_t read_bytes = read(sockfd, buf, sizeof(buf));
        if (read_bytes > 0)
        {
            printf("message from server: %s\n", buf);
        }
        else if (read_bytes == 0)
        {
            printf("server socket dissconnect\n");
            break;
        }
        else
        {
            close(sockfd);
            errif(true, "socked read error");
        }
    }

    return 0;
}