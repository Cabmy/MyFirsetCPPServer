#pragma once
#include <sys/epoll.h>
#include <vector>

class Epoll
{
private:
    int epfd;
    std::vector<epoll_event> events;

public:
    Epoll();
    ~Epoll();

    void addFd(int fd, uint32_t op);
    std::vector<epoll_event> activeEvents(int timeout = -1);
};