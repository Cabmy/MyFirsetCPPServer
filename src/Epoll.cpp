#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "Epoll.h"
#include "util.h"
#include "Log.h"
#include "Channel.h"

static constexpr int kMaxEvents = 4096;

Epoll::Epoll() : epfd_(-1), events_(kMaxEvents)
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

std::vector<Channel *> Epoll::activeChannels(int timeout)
{
    std::vector<Channel *> activeChannels;
    int nfds = epoll_wait(epfd_, events_.data(), kMaxEvents, timeout);
    if (nfds == -1) {
        if (errno == EINTR) return {};
        // A transient epoll_wait error must NOT kill the whole server; log and
        // return an empty set so the event loop keeps running.
        LOG_ERROR("epoll_wait error: %s", strerror(errno));
        return {};
    }

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
        if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) == -1)
        {
            // Under a short-connection storm an fd number can be recycled
            // while a prior connection's registration lingers -> EEXIST.
            // Take the fd over with MOD instead of aborting the whole server.
            if (errno == EEXIST)
                epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
            else
                LOG_ERROR("epoll ADD fd=%d failed: %s", fd, strerror(errno));
        }
        channel->setInEpoll();
    }
    else
    {
        if (epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) == -1)
            LOG_ERROR("epoll MOD fd=%d failed: %s", fd, strerror(errno));
    }
}