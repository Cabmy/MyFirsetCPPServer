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
    events_ |= EPOLLIN | EPOLLPRI | EPOLLRDHUP;
    loop_->updateChannel(this);
}

// 一次性注册 IN|PRI|RDHUP|ET, 避免 ADD+MOD 双重 epoll_ctl
void Channel::enableReadingET()
{
    events_ |= EPOLLIN | EPOLLPRI | EPOLLRDHUP | EPOLLET;
    loop_->updateChannel(this);
}

// 发送缓冲区一次没写完时, 注册 EPOLLOUT 等待可写再续写
void Channel::enableWriting()
{
    events_ |= EPOLLOUT;
    loop_->updateChannel(this);
}

// 缓冲区写完后取消 EPOLLOUT, 避免 busy loop (ET 下 EPOLLOUT 常态可写)
void Channel::disableWriting()
{
    events_ &= ~EPOLLOUT;
    loop_->updateChannel(this);
}

void Channel::handleEvent()
{
    // 先处理可写: 续写未发送完的响应
    if (revent_ & EPOLLOUT)
    {
        if (writeCallback_)
            writeCallback_();
        return; // 写事件与读事件互斥处理; 写完后由读路径感知对端关闭
    }
    if (revent_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP | EPOLLHUP | EPOLLERR))
    {
        if (callback_)
            callback_();
    }
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

void Channel::useET()
{
    events_ |= EPOLLET;
    loop_->updateChannel(this);
}

void Channel::setRevents(uint32_t ev)
{
    revent_ = ev;
}

void Channel::setCallback(std::function<void()> cb)
{
    callback_ = cb;
}

void Channel::setWriteCallback(std::function<void()> cb)
{
    writeCallback_ = cb;
}