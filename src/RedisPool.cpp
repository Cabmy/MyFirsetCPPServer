#ifdef USE_REDIS

#include "RedisPool.h"
#include "Config.h"
#include "Log.h"
#include <stdio.h>
#include <chrono>

RedisPool &RedisPool::getInstance()
{
    static RedisPool instance;
    return instance;
}

RedisPool::RedisPool()
    : maxConn_(config::kRedisMaxConn), host_(config::redisHost()), port_(config::redisPort())
{
    int created = 0;
    for (int i = 0; i < maxConn_; ++i)
    {
        redisContext *conn = createConn();
        if (conn)
        {
            pool_.push(conn);
            ++created;
        }
    }

    if (created == 0)
    {
        LOG_WARN("RedisPool: no connections created; check Redis is running on %s:%u", host_.c_str(), port_);
    }
    else
    {
        LOG_INFO("RedisPool: initialized with %d connections", created);
    }
}

// Open a single new Redis connection. Returns nullptr on failure.
redisContext *RedisPool::createConn()
{
    struct timeval timeout = {config::kRedisConnectTimeoutMs / 1000,
                              (config::kRedisConnectTimeoutMs % 1000) * 1000};
    redisContext *conn = redisConnectWithTimeout(host_.c_str(), port_, timeout);
    if (!conn || conn->err)
    {
        if (conn)
        {
            LOG_ERROR("RedisPool: connect failed: %s", conn->errstr);
            redisFree(conn);
        }
        return nullptr;
    }
    return conn;
}

// A command returned NULL reply => the connection is broken and must not be
// reused. Free it and try to open a replacement so the pool does not shrink.
void RedisPool::discardBrokenConn(redisContext *conn)
{
    if (conn)
        redisFree(conn);
    redisContext *fresh = createConn();
    if (fresh)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        pool_.push(fresh);
        cv_.notify_one();
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
    if (!cv_.wait_for(lock, std::chrono::seconds(config::kPoolAcquireTimeoutSec), [this]
                       { return !pool_.empty(); }))
    {
        LOG_ERROR("RedisPool: timeout waiting for connection");
        return nullptr;
    }

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

std::string RedisPool::get(const std::string &key, RedisStatus *status)
{
    auto setStatus = [status](RedisStatus s)
    { if (status) *status = s; };

    redisContext *conn = getConn();
    if (!conn)
    {
        setStatus(RedisStatus::Error);
        return "";
    }

    redisReply *reply = (redisReply *)redisCommand(conn, "GET %s", key.c_str());
    if (!reply)
    {
        // Broken connection: do not return it to the pool
        discardBrokenConn(conn);
        setStatus(RedisStatus::Error);
        return "";
    }

    std::string result;
    RedisStatus st;
    if (reply->type == REDIS_REPLY_STRING)
    {
        result.assign(reply->str, reply->len);
        st = RedisStatus::Ok;
    }
    else
    {
        st = RedisStatus::NotFound; // nil or unexpected type
    }
    freeReplyObject(reply);
    releaseConn(conn);
    setStatus(st);
    return result;
}

bool RedisPool::setex(const std::string &key, int ttl, const std::string &value)
{
    redisContext *conn = getConn();
    if (!conn)
        return false;

    redisReply *reply = (redisReply *)redisCommand(conn, "SETEX %s %d %s",
                                                   key.c_str(), ttl, value.c_str());
    if (!reply)
    {
        discardBrokenConn(conn);
        return false;
    }
    bool ok = (reply->type != REDIS_REPLY_ERROR);
    freeReplyObject(reply);
    releaseConn(conn);
    return ok;
}

RedisStatus RedisPool::del(const std::string &key)
{
    redisContext *conn = getConn();
    if (!conn)
        return RedisStatus::Error;

    redisReply *reply = (redisReply *)redisCommand(conn, "DEL %s", key.c_str());
    if (!reply)
    {
        discardBrokenConn(conn);
        return RedisStatus::Error;
    }
    RedisStatus st = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0)
                         ? RedisStatus::Ok
                         : RedisStatus::NotFound;
    freeReplyObject(reply);
    releaseConn(conn);
    return st;
}

#endif // USE_REDIS
