#pragma once
#include <functional>

class Epoll;
class Channel;
class ThreadPool;

class EventLoop
{
private:
    Epoll *ep_;
    ThreadPool *ThreadPool_;
    bool quit_;

public:
    EventLoop();
    ~EventLoop();

    void loop();
    void updateChannel(Channel *);

    void addThread(std::function<void()>);
};