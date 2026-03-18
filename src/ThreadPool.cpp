#include "ThreadPool.h"
#include <stdexcept>

ThreadPool::ThreadPool(int size) : stop_(false)
{
    for (int i = 0; i < size; ++i)
    {
        threads_.emplace_back(std::thread([this]() // 捕获this以访问当前类变量
                                          {
            while(true){
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(tasks_mtx_);// 离开{}后析构自动解锁
                    cv_.wait(lock, [this](){
                        return stop_ || !tasks_.empty();
                    }) ;// 挂起，等待任务

                    if(stop_ && tasks_.empty()){
                        return;
                    }

                    task = tasks_.front();
                    tasks_.pop();
                }

                task();
            } }));
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(tasks_mtx_);
        stop_ = true; // stop被多个线程共享读写，需要加锁
    }

    cv_.notify_all();
    for (std::thread &th : threads_)
    {
        if (th.joinable())
        {
            th.join(); // 阻塞当前线程直到目标线程执行结束
        }
    }
}

void ThreadPool::add(std::function<void()> func)
{
    {
        std::unique_lock<std::mutex> lock(tasks_mtx_);
        if (stop_)
        {
            throw std::runtime_error("ThreadPool already stop, can not add task anymore");
        }
        tasks_.emplace(func);
    }

    cv_.notify_one();
}