#include "EventLoop.h"
#include "Epoll.h"
#include "Channel.h"
#include <vector>

EventLoop::EventLoop() : ep_(nullptr), quit_(false)
{
    ep_ = std::make_unique<Epoll>();
}

EventLoop::~EventLoop()
{
}

void EventLoop::loop()
{
    // 1s poll timeout so the loop periodically wakes to run housekeeping
    // (idle-connection sweep / shutdown check) and can observe quit_.
    while (!quit_.load())
    {
        std::vector<Channel *> chs = ep_->activeChannels(1000);
        for (Channel *ch : chs)
        {
            ch->handleEvent();
        }
        if (loopCallback_)
            loopCallback_();
    }
}

void EventLoop::updateChannel(Channel *ch)
{
    ep_->updateChannel(ch);
}
