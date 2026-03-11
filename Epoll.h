#pragma once
#include <sys/epoll.h>
#include <vector>

class Channel;

class Epoll
{
private:
    int epfd_;
    std::vector<epoll_event> events_;

public:
    Epoll();
    ~Epoll();

    // void addFd(int fd, uint32_t op); --被updateChannel 取代
    // std::vector<epoll_event> activeEvents(int timeout = -1);
    std::vector<Channel *> activeChannels(int timeout = -1);
    void updateChannel(Channel *channel);
};