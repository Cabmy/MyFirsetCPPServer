#include "src/Socket.h"
#include "src/Buffer.h"
#include "src/Connection.h"
#include "src/InetAddress.h"
#include "src/util.h"
#include <iostream>
#include <unistd.h>
#include <string.h>

int main()
{
    // int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    // struct sockaddr_in serv_addv;
    // bzero(&serv_addv, sizeof(serv_addv));
    // serv_addv.sin_family = AF_INET;
    // serv_addv.sin_addr.s_addr = inet_addr("127.0.0.1");
    // serv_addv.sin_port = htons(8888);

    Socket *sock = new Socket();
    InetAddress *addr = new InetAddress("127.0.0.1", 8888);
    sock->connect(addr);

    int sockfd = sock->getFd();

    Buffer *readBuffer = new Buffer();
    Buffer *sendBuffer = new Buffer();

    while (true)
    {
        sendBuffer->getline();
        ssize_t write_bytes = write(sockfd, sendBuffer->c_str(), sendBuffer->size());
        if (write_bytes == -1)
        {
            errif(true, "server socket disconnect");
            break;
        }

        // 读取从服务器写的值
        int already_read = 0;
        char buf[1024];
        while (true)
        {
            bzero(&buf, sizeof(buf));
            ssize_t read_bytes = read(sockfd, buf, sizeof(buf));
            if (read_bytes > 0)
            {
                readBuffer->append(buf, read_bytes);
                already_read += read_bytes;
            }
            else if (read_bytes == 0)
            {
                printf("server disconnected.\n");
                exit(EXIT_SUCCESS);
            }
            if (already_read >= sendBuffer->size())
            {
                printf("message from server: %s\n", readBuffer->c_str());
                break;
            }
        }
        readBuffer->clear();
    }

    delete addr;
    delete sock;
    return 0;
}