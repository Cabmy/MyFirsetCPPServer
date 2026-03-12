#include <unistd.h>
#include <string.h>
#include "Epoll.h"
#include "util.h"
#include "Channel.h"

#define MAX_EVENTS_ 1024

Epoll::Epoll() : epfd_(-1), events_(MAX_EVENTS_)
{
    epfd_ = epoll_create1(0);
    errif(epfd_ == -1, "epoll create error");
}

Epoll::~Epoll()
{
    if (epfd_ != -1)
    {
        close(epfd_);
        epfd_ = -1;
    }
}

// void Epoll::addFd(int fd, uint32_t op)
// {
//     struct epoll_event ev;
//     bzero(&ev, sizeof(ev));

//     ev.data.fd = fd;
//     ev.events = op;
//     errif(epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) == -1, "epoll add event error");
// }

std::vector<Channel *> Epoll::activeChannels(int timeout)
{
    std::vector<Channel *> activeChannels;
    int nfds = epoll_wait(epfd_, events_.data(), MAX_EVENTS_, timeout);
    errif(nfds == -1, "epoll wait error");

    for (int i = 0; i < nfds; ++i)
    {
        Channel *ch = (Channel *)events_[i].data.ptr;
        ch->setRevents(events_[i].events);
        activeChannels.push_back(ch);
    }

    return activeChannels;
}

void Epoll::updateChannel(Channel *channel)
{
    int fd = channel->getFd();
    struct epoll_event ev;
    bzero(&ev, sizeof(ev));

    ev.data.ptr = channel;
    ev.events = channel->getEvents();
    if (!channel->getInEpoll())
    {
        errif(epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) == -1, "epoll add error");
        channel->setInEpoll();
    }
    else
    {
        errif(epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) == -1, "epoll add error");
    }
}