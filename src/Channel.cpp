#include "Channel.h"
#include "EventLoop.h"

Channel::Channel(EventLoop *loop, int fd) : loop_(loop), fd_(fd), events_(0), revent_(0), inEpoll_(false)
{
}

Channel::~Channel()
{
}

// 注册到Epoll监听
void Channel::enableReading()
{
    events_ = EPOLLIN | EPOLLET;
    loop_->updateChannel(this);
}

void Channel::handleEvent()
{
    loop_->addThread(callback_);
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

void Channel::setCallback(std::function<void()> cb)
{
    callback_ = cb;
}