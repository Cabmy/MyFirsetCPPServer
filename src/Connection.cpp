#include "Connection.h"
#include "Socket.h"
#include "Channel.h"
#include "Buffer.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <string>

Connection::Connection(EventLoop *loop, std::unique_ptr<Socket> sock) : loop_(loop), sock_(std::move(sock))
{
    channel_ = std::make_unique<Channel>(loop, sock_->getFd());
    channel_->enableReading();
    channel_->useET();
    std::function<void()> cb = std::bind(&Connection::handleMessage, this);
    channel_->setCallback(cb);
}

Connection::~Connection()
{
}

std::string Connection::parseUrl(const std::string &request)
{
    // 解析请求行: "GET /path HTTP/1.1\r\n..."
    size_t pos1 = request.find(' ');
    if (pos1 == std::string::npos) return "/";
    size_t pos2 = request.find(' ', pos1 + 1);
    if (pos2 == std::string::npos) return "/";
    return request.substr(pos1 + 1, pos2 - pos1 - 1);
}

std::string Connection::getResponse(const std::string &url)
{
    if (url == "/") {
        return "<html><body><h1>Welcome to My HTTP Server</h1>"
               "<p>This is a simple HTTP server built with epoll reactor pattern.</p>"
               "<p>Try: <a href=\"/hello\">/hello</a></p>"
               "</body></html>";
    } else if (url == "/hello") {
        return "<html><body><h1>Hello!</h1></body></html>";
    } else {
        return "<html><body><h1>404 Not Found</h1></body></html>";
    }
}

void Connection::handleMessage()
{
    int sockfd = sock_->getFd();
    Buffer readBuffer;
    char buf[1024];
    while (true)
    {
        bzero(&buf, sizeof(buf));
        ssize_t read_bytes = read(sockfd, buf, sizeof(buf));
        if (read_bytes > 0)
        {
            readBuffer.append(buf, read_bytes);
        }
        else if (read_bytes == -1 && errno == EINTR)
        {
            printf("continue reading\n");
            continue;
        }
        else if (read_bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            if (readBuffer.size() == 0)
            {
                break;
            }
            printf("finish reading once, errno: %d\n", errno);
            printf("message from client fd %d: %s\n", sockfd, readBuffer.c_str());

            // === 原echo逻辑（已注释）===
            // ssize_t bytes_written = 0;
            // ssize_t total = readBuffer.size();
            // const char* data = readBuffer.c_str();
            // while (bytes_written < total) {
            //     ssize_t n = write(sockfd, data + bytes_written, total - bytes_written);
            //     if (n > 0) {
            //         bytes_written += n;
            //     } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            //         break;
            //     } else if (n == -1 && errno == EINTR) {
            //         continue;
            //     } else {
            //         deleteConnectionCallback_(sock_->getFd());
            //         return;
            //     }
            // }

            // === HTTP响应逻辑 ===
            std::string request(readBuffer.c_str());
            std::string url = parseUrl(request);
            std::string body = getResponse(url);
            std::string statusLine = (url == "/" || url == "/hello")
                ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n";
            std::string response = statusLine +
                "Content-Type: text/html\r\n"
                "Content-Length: " + std::to_string(body.size()) + "\r\n"
                "Connection: close\r\n\r\n" + body;

            const char *data = response.c_str();
            ssize_t total = response.size();
            ssize_t bytes_written = 0;
            while (bytes_written < total) {
                ssize_t n = write(sockfd, data + bytes_written, total - bytes_written);
                if (n > 0) {
                    bytes_written += n;
                } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    break;
                } else if (n == -1 && errno == EINTR) {
                    continue;
                } else {
                    deleteConnectionCallback_(sock_->getFd());
                    return;
                }
            }

            readBuffer.clear();
            break;
        }
        else if (read_bytes == 0)
        {
            printf("EOF! client fd %d disconnected\n", sockfd);
            deleteConnectionCallback_(sock_->getFd());
            break;
        }
        else
        {
            printf("Connection reset by peer\n");
            deleteConnectionCallback_(sock_->getFd());
            break;
        }
    }
}

// === 原echo函数（已注释保留）===
// void Connection::echo()
// {
//     int sockfd = sock_->getFd();
//     Buffer readBuffer;
//     char buf[1024];
//     while (true)
//     {
//         bzero(&buf, sizeof(buf));
//         ssize_t read_bytes = read(sockfd, buf, sizeof(buf));
//         if (read_bytes > 0)
//         {
//             readBuffer.append(buf, read_bytes);
//         }
//         else if (read_bytes == -1 && errno == EINTR)
//         {
//             printf("continue reading\n");
//             continue;
//         }
//         else if (read_bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
//         {
//             if (readBuffer.size() == 0)
//             {
//                 break;
//             }
//             printf("finish reading once, errno: %d\n", errno);
//             printf("message from client fd %d: %s\n", sockfd, readBuffer.c_str());
//             ssize_t bytes_written = 0;
//             ssize_t total = readBuffer.size();
//             const char* data = readBuffer.c_str();
//             while (bytes_written < total) {
//                 ssize_t n = write(sockfd, data + bytes_written, total - bytes_written);
//                 if (n > 0) {
//                     bytes_written += n;
//                 } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
//                     break;
//                 } else if (n == -1 && errno == EINTR) {
//                     continue;
//                 } else {
//                     deleteConnectionCallback_(sock_->getFd());
//                     return;
//                 }
//             }
//             readBuffer.clear();
//             break;
//         }
//         else if (read_bytes == 0)
//         {
//             printf("EOF! client fd %d disconnected\n", sockfd);
//             deleteConnectionCallback_(sock_->getFd());
//             break;
//         }
//         else
//         {
//             printf("Connection reset by peer\n");
//             deleteConnectionCallback_(sock_->getFd());
//             break;
//         }
//     }
// }

void Connection::setDeleteConnectionCallback(std::function<void(int)> cb)
{
    deleteConnectionCallback_ = cb;
}