#ifdef USE_REDIS

#include "RedisPool.h"
#include <stdio.h>

// ====== Redis config (modify these for your environment) ======
static const char *REDIS_HOST = "127.0.0.1";
static const unsigned int REDIS_PORT = 6379;
static const int REDIS_MAX_CONN = 8;

RedisPool &RedisPool::getInstance()
{
    static RedisPool instance;
    return instance;
}

RedisPool::RedisPool()
    : maxConn_(REDIS_MAX_CONN), host_(REDIS_HOST), port_(REDIS_PORT)
{
    int created = 0;
    for (int i = 0; i < maxConn_; ++i)
    {
        struct timeval timeout = {1, 500000}; // 1.5s
        redisContext *conn = redisConnectWithTimeout(host_.c_str(), port_, timeout);
        if (!conn || conn->err)
        {
            if (conn)
            {
                fprintf(stderr, "RedisPool: connect failed: %s\n", conn->errstr);
                redisFree(conn);
            }
            continue;
        }

        pool_.push(conn);
        ++created;
    }

    if (created == 0)
    {
        fprintf(stderr, "RedisPool: WARNING - no connections created. "
                        "Check Redis is running on %s:%u\n", host_.c_str(), port_);
    }
    else
    {
        printf("RedisPool: initialized with %d connections\n", created);
    }
}

RedisPool::~RedisPool()
{
    std::lock_guard<std::mutex> lock(mtx_);
    while (!pool_.empty())
    {
        redisFree(pool_.front());
        pool_.pop();
    }
}

redisContext *RedisPool::getConn()
{
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this]
             { return !pool_.empty(); });

    redisContext *conn = pool_.front();
    pool_.pop();
    return conn;
}

void RedisPool::releaseConn(redisContext *conn)
{
    if (!conn)
        return;

    std::lock_guard<std::mutex> lock(mtx_);
    pool_.push(conn);
    cv_.notify_one();
}

std::string RedisPool::get(const std::string &key)
{
    redisContext *conn = getConn();
    redisReply *reply = (redisReply *)redisCommand(conn, "GET %s", key.c_str());
    std::string result;
    if (reply && reply->type == REDIS_REPLY_STRING)
    {
        result = std::string(reply->str, reply->len);
    }
    if (reply)
        freeReplyObject(reply);
    releaseConn(conn);
    return result;
}

void RedisPool::setex(const std::string &key, int ttl, const std::string &value)
{
    redisContext *conn = getConn();
    redisReply *reply = (redisReply *)redisCommand(conn, "SETEX %s %d %s",
                                                   key.c_str(), ttl, value.c_str());
    if (reply)
        freeReplyObject(reply);
    releaseConn(conn);
}

void RedisPool::del(const std::string &key)
{
    redisContext *conn = getConn();
    redisReply *reply = (redisReply *)redisCommand(conn, "DEL %s", key.c_str());
    if (reply)
        freeReplyObject(reply);
    releaseConn(conn);
}

#endif // USE_REDIS
