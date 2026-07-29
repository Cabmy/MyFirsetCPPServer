#pragma once

#ifdef USE_REDIS

#include <hiredis/hiredis.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>

// Result of a Redis operation, so callers can tell an infrastructure failure
// (pool exhausted / connection broken) apart from a normal cache miss.
enum class RedisStatus
{
    Ok,       // command succeeded (value present for get)
    NotFound, // command succeeded but key does not exist
    Error     // could not talk to Redis (timeout / broken connection)
};

class RedisPool
{
public:
    static RedisPool &getInstance();

    redisContext *getConn();
    void releaseConn(redisContext *conn);

    // Convenience methods (thread-safe, acquire+release internally).
    // get(): on Error/NotFound returns ""; inspect *status to distinguish.
    std::string get(const std::string &key, RedisStatus *status = nullptr);
    bool setex(const std::string &key, int ttl, const std::string &value); // true on success
    RedisStatus del(const std::string &key);

private:
    RedisPool();
    ~RedisPool();
    RedisPool(const RedisPool &) = delete;
    RedisPool &operator=(const RedisPool &) = delete;

    redisContext *createConn();               // open one new connection (nullptr on failure)
    void discardBrokenConn(redisContext *conn); // free a broken conn, try to replace it in the pool

    std::queue<redisContext *> pool_;
    std::mutex mtx_;
    std::condition_variable cv_;
    int maxConn_;

    std::string host_;
    unsigned int port_;
};

#endif // USE_REDIS
