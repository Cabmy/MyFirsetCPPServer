#include "EventLoop.h"
#include "Epoll.h"
#include "Channel.h"
#include "ThreadPool.h"
#include <vector>

EventLoop::EventLoop() : ep_(nullptr), ThreadPool_(nullptr), quit_(false)
{
    ep_ = new Epoll();
    ThreadPool_ = new ThreadPool();
}

EventLoop::~EventLoop()
{
    delete ep_;
    delete ThreadPool_;
}

void EventLoop::loop()
{
    while (!quit_)
    {
        std::vector<Channel *> chs;
        chs = ep_->activeChannels();
        for (Channel *ch : chs)
        {
            ch->handleEvent();
        }
    }
}

void EventLoop::updateChannel(Channel *ch)
{
    ep_->updateChannel(ch);
}

void EventLoop::addThread(std::function<void()> func)
{
    ThreadPool_->add(func);
}