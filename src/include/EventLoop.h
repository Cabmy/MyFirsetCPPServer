#pragma once
#include <functional>
#include <memory>
#include <atomic>

class Epoll;
class Channel;

class EventLoop
{
private:
    std::unique_ptr<Epoll> ep_;
    std::atomic<bool> quit_;
    std::function<void()> loopCallback_; // run once per loop iteration (housekeeping)

public:
    EventLoop();
    ~EventLoop();

    void loop();
    void quit() { quit_.store(true); }
    void setLoopCallback(std::function<void()> cb) { loopCallback_ = std::move(cb); }
    void updateChannel(Channel *);
};