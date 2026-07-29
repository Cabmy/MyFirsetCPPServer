#pragma once
#include <map>
#include <vector>
#include <memory>
#include <mutex>

class EventLoop;
class Socket;
class Acceptor;
class Connection;
class ThreadPool;

class Server
{
private:
    EventLoop *mainReactor_;
    std::unique_ptr<Acceptor> acceptor_;
    std::map<int, std::unique_ptr<Connection>> connections_;
    std::mutex connMtx_;
    std::vector<std::unique_ptr<EventLoop>> subReactors_;
    std::unique_ptr<ThreadPool> thPool_;
    std::unique_ptr<ThreadPool> dbPool_;

public:
    Server(EventLoop *lp);
    ~Server();

    void newConnection(std::unique_ptr<Socket>);
    void deleteConnection(int);

    void stop();      // quit main + all sub reactors (graceful shutdown)
    void sweepIdle(); // shutdown() connections idle beyond the configured timeout
};