#include "MysqlPool.h"
#include "Config.h"
#include "Log.h"
#include <stdio.h>
#include <stdexcept>
#include <chrono>

MysqlPool &MysqlPool::getInstance()
{
    static MysqlPool instance;
    return instance;
}

MysqlPool::MysqlPool()
    : maxConn_(config::kDbMaxConn), freeConn_(0),
      host_(config::dbHost()), user_(config::dbUser()), passwd_(config::dbPasswd()),
      dbName_(config::dbName()), port_(config::dbPort())
{
    for (int i = 0; i < maxConn_; ++i)
    {
        MYSQL *conn = mysql_init(nullptr);
        if (!conn)
        {
            LOG_ERROR("MysqlPool: mysql_init failed");
            continue;
        }

        // Keep the init handle: on failure mysql_real_connect returns NULL
        // and the error must be read (and the handle freed) via the original
        if (!mysql_real_connect(conn,
                                host_.c_str(), user_.c_str(), passwd_.c_str(),
                                dbName_.c_str(), port_, nullptr, 0))
        {
            LOG_ERROR("MysqlPool: mysql_real_connect failed: %s", mysql_error(conn));
            mysql_close(conn);
            continue;
        }

        // Set UTF-8 charset
        mysql_set_character_set(conn, "utf8mb4");

        pool_.push(conn);
        ++freeConn_;
    }

    if (freeConn_ == 0)
    {
        LOG_WARN("MysqlPool: no connections created; check MySQL is running and credentials");
    }
    else
    {
        LOG_INFO("MysqlPool: initialized with %d connections", freeConn_);
    }
}

MysqlPool::~MysqlPool()
{
    std::lock_guard<std::mutex> lock(mtx_);
    while (!pool_.empty())
    {
        mysql_close(pool_.front());
        pool_.pop();
    }
}

MYSQL *MysqlPool::getConn()
{
    std::unique_lock<std::mutex> lock(mtx_);
    if (!cv_.wait_for(lock, std::chrono::seconds(config::kPoolAcquireTimeoutSec), [this]
                       { return !pool_.empty(); }))
    {
        LOG_ERROR("MysqlPool: timeout waiting for connection");
        return nullptr;
    }

    MYSQL *conn = pool_.front();
    pool_.pop();
    --freeConn_;
    return conn;
}

void MysqlPool::releaseConn(MYSQL *conn)
{
    if (!conn)
        return;

    std::lock_guard<std::mutex> lock(mtx_);
    pool_.push(conn);
    ++freeConn_;
    cv_.notify_one();
}
