#pragma once

#ifdef USE_REDIS

#include <hiredis/hiredis.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>

class RedisPool
{
public:
    static RedisPool &getInstance();

    redisContext *getConn();
    void releaseConn(redisContext *conn);

    // Convenience methods (thread-safe, acquire+release internally)
    std::string get(const std::string &key);
    void setex(const std::string &key, int ttl, const std::string &value);
    void del(const std::string &key);

private:
    RedisPool();
    ~RedisPool();
    RedisPool(const RedisPool &) = delete;
    RedisPool &operator=(const RedisPool &) = delete;

    std::queue<redisContext *> pool_;
    std::mutex mtx_;
    std::condition_variable cv_;
    int maxConn_;

    std::string host_;
    unsigned int port_;
};

#endif // USE_REDIS
