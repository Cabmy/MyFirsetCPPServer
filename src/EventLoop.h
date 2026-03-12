#pragma once

class Epoll;
class Channel;

class EventLoop
{
private:
    Epoll *ep_;
    bool quit_;

public:
    EventLoop();
    ~EventLoop();

    void loop();
    void updateChannel(Channel *);
};