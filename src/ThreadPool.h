#pragma once
#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>

class ThreadPool
{
private:
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> tasks_;
    std::mutex tasks_mtx_;
    std::condition_variable cv_;
    bool stop_;

public:
    ThreadPool(int size = std::thread::hardware_concurrency());
    ~ThreadPool();

    void add(std::function<void()>);
};