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

    std::vector<Channel *> activeChannels(int timeout = -1);
    void updateChannel(Channel *channel);
};