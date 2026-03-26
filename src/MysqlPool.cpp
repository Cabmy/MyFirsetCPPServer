#include "MysqlPool.h"
#include <stdio.h>
#include <stdexcept>

// ====== Database config (modify these for your environment) ======
static const char *DB_HOST = "127.0.0.1";
static const char *DB_USER = "root";
static const char *DB_PASSWD = "admin";
static const char *DB_NAME = "myserver";
static const unsigned int DB_PORT = 3306;
static const int DB_MAX_CONN = 8;

MysqlPool &MysqlPool::getInstance()
{
    static MysqlPool instance;
    return instance;
}

MysqlPool::MysqlPool()
    : maxConn_(DB_MAX_CONN), freeConn_(0),
      host_(DB_HOST), user_(DB_USER), passwd_(DB_PASSWD),
      dbName_(DB_NAME), port_(DB_PORT)
{
    for (int i = 0; i < maxConn_; ++i)
    {
        MYSQL *conn = mysql_init(nullptr);
        if (!conn)
        {
            fprintf(stderr, "MysqlPool: mysql_init failed\n");
            continue;
        }

        conn = mysql_real_connect(conn,
                                  host_.c_str(), user_.c_str(), passwd_.c_str(),
                                  dbName_.c_str(), port_, nullptr, 0);
        if (!conn)
        {
            fprintf(stderr, "MysqlPool: mysql_real_connect failed: %s\n",
                    mysql_error(conn));
            continue;
        }

        // Set UTF-8 charset
        mysql_set_character_set(conn, "utf8mb4");

        pool_.push(conn);
        ++freeConn_;
    }

    if (freeConn_ == 0)
    {
        fprintf(stderr, "MysqlPool: WARNING - no connections created. "
                        "Check MySQL is running and credentials are correct.\n");
    }
    else
    {
        printf("MysqlPool: initialized with %d connections\n", freeConn_);
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
    cv_.wait(lock, [this]
             { return !pool_.empty(); });

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
