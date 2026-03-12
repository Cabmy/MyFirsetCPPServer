#include "EventLoop.h"
#include "Epoll.h"
#include "Channel.h"
#include <vector>

EventLoop::EventLoop() : ep_(nullptr), quit_(false)
{
    ep_ = new Epoll();
}

EventLoop::~EventLoop()
{
    delete ep_;
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