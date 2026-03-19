#pragma once
#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <future>

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

    // void add(std::function<void()>);
    template <class F, class... Args>
    auto add(F &&f, Args &&...args)
        -> std::future<typename std::result_of<F(Args...)>::type>;
};

template <class F, class... Args>
auto ThreadPool::add(F &&f, Args &&...args)
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();

    {
        std::unique_lock<std::mutex> lock(tasks_mtx_);

        if (stop_)
        {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }

        tasks_.emplace([task]()
                       { (*task)(); });
    }

    cv_.notify_one();
    return res;
}