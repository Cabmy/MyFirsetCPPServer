#pragma once
#include <sys/epoll.h>

class Epoll;

class Channel
{
private:
    Epoll *ep_; // 挂在哪个Epoll树上
    int fd_;
    uint32_t events_; // 期望监听事件
    uint32_t revent_; // 实际发生事件
    bool inEpoll_;    // 是否挂载

public:
    Channel(Epoll *ep, int fd);
    ~Channel();

    void enableReading();

    int getFd();
    uint32_t getEvents();
    uint32_t getRevents();
    bool getInEpoll();
    void setInEpoll();

    void setRevents(uint32_t revents);
};