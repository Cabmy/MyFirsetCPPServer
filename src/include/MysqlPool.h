#pragma once
#include <mysql/mysql.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>

class MysqlPool
{
public:
    static MysqlPool &getInstance();

    MYSQL *getConn();
    void releaseConn(MYSQL *conn);

private:
    MysqlPool();
    ~MysqlPool();
    MysqlPool(const MysqlPool &) = delete;
    MysqlPool &operator=(const MysqlPool &) = delete;

    std::queue<MYSQL *> pool_;
    std::mutex mtx_;
    std::condition_variable cv_;
    int maxConn_;

    std::string host_;
    std::string user_;
    std::string passwd_;
    std::string dbName_;
    unsigned int port_;
};

// RAII wrapper: auto-release connection when out of scope
class MysqlGuard
{
public:
    MysqlGuard() : conn_(MysqlPool::getInstance().getConn()) {}
    ~MysqlGuard()
    {
        if (conn_)
            MysqlPool::getInstance().releaseConn(conn_);
    }
    MYSQL *get() { return conn_; }
    MysqlGuard(const MysqlGuard &) = delete;
    MysqlGuard &operator=(const MysqlGuard &) = delete;

private:
    MYSQL *conn_;
};
