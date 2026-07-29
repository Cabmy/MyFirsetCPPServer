#pragma once
#include <sys/epoll.h>
#include <functional>

class EventLoop;

class Channel
{
private:
    EventLoop *loop_;
    int fd_;
    uint32_t events_; // 期望监听事件
    uint32_t revent_; // 实际发生事件
    bool inEpoll_;    // 是否挂载
    std::function<void()> callback_;      // 读事件回调
    std::function<void()> writeCallback_; // 写事件回调 (EPOLLOUT)

public:
    Channel(EventLoop *loop, int fd);
    ~Channel();

    void enableReading();
    void enableReadingET();
    void enableWriting();  // 注册 EPOLLOUT (发送缓冲区未写完时)
    void disableWriting(); // 取消 EPOLLOUT (缓冲区写完后)
    void handleEvent();

    int getFd();
    uint32_t getEvents();
    uint32_t getRevents();
    bool getInEpoll();
    void setInEpoll();
    void useET();

    void setRevents(uint32_t revents);
    void setCallback(std::function<void()>);
    void setWriteCallback(std::function<void()>);
};