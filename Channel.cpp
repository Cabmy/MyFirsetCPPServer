#include "Channel.h"
#include "Epoll.h"

Channel::Channel(Epoll *ep, int fd) : ep_(ep), fd_(fd), events_(0), revent_(0), inEpoll_(false)
{
}

Channel::~Channel()
{
}

void Channel::enableReading()
{
    events_ = EPOLLIN | EPOLLET;
    ep_->updateChannel(this);
}

int Channel::getFd()
{
    return fd_;
}

uint32_t Channel::getEvents()
{
    return events_;
}

uint32_t Channel::getRevents()
{
    return revent_;
}

bool Channel::getInEpoll()
{
    return inEpoll_;
}

void Channel::setInEpoll()
{
    inEpoll_ = true;
}

void Channel::setRevents(uint32_t ev)
{
    revent_ = ev;
}