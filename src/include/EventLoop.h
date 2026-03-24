#pragma once
#include <functional>
#include <memory>

class Epoll;
class Channel;

class EventLoop
{
private:
    std::unique_ptr<Epoll> ep_;
    bool quit_;

public:
    EventLoop();
    ~EventLoop();

    void loop();
    void updateChannel(Channel *);
};