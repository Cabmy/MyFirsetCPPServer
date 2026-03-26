# MyFirstCPPServer

基于主从 Reactor 多线程模式的高并发 HTTP 服务器，支持 MySQL 用户注册/登录 + Redis 缓存加速。

## 技术栈

- 语言：C++14
- I/O多路复用：Linux epoll（边缘触发 ET 模式 + 非阻塞 I/O）
- 并发模型：主从 Reactor 多线程模式（1个主Reactor接收连接 + N个从Reactor处理IO，N = CPU核心数）
- 线程池：基于 std::packaged_task + std::future，支持任意可调用对象和返回值获取
- 数据库：MySQL 连接池（单例模式，mutex + condition_variable，RAII 自动归还）
- 缓存：Redis 连接池（hiredis 同步客户端，用户缓存 + 会话Token管理）
- 内存管理：std::unique_ptr 管理 Socket、Channel、Connection 等对象的生命周期（RAII）
- 构建工具：CMake

## 架构

```
                    ┌─────────────────────────┐
                    │     Main Reactor         │
                    │  (EventLoop + Acceptor)  │
                    │      accept 新连接        │
                    └──────────┬──────────────┘
                               │ fd % N 轮询分配
              ┌────────────────┼────────────────┐
              ▼                ▼                 ▼
     ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
     │ Sub Reactor 0│ │ Sub Reactor 1│ │ Sub Reactor N│
     │  EventLoop   │ │  EventLoop   │ │  EventLoop   │
     │  (thPool_)   │ │  (thPool_)   │ │  (thPool_)   │
     └──────┬───────┘ └──────┬───────┘ └──────┬───────┘
            │                │                 │
            │  GET 请求直接处理，POST 请求提交到 DB 线程池
            │                │                 │
            └────────────────┼─────────────────┘
                             ▼
                    ┌──────────────────┐
                    │  DB ThreadPool   │
                    │   (dbPool_, 4)   │
                    ├──────────────────┤
                    │  MySQL 连接池     │
                    │  Redis 连接池     │
                    └──────────────────┘
```

**两个线程池**：
- `thPool_`（N线程）：永久运行从Reactor的 `EventLoop::loop()`，处理网络IO
- `dbPool_`（4线程）：执行阻塞的 MySQL/Redis 操作，避免阻塞Reactor线程

## 各类的作用

- **Server**：总调度器。持有主Reactor、从Reactor数组、Reactor线程池（`thPool_`）、DB线程池（`dbPool_`）、连接表（fd → Connection）。负责接收新连接并分配到从Reactor，以及删除断开的连接。
- **EventLoop**：事件循环，即一个Reactor。内部持有一个Epoll，不断调用 epoll_wait 获取就绪事件，然后逐个调用 Channel 的回调。每个线程跑一个EventLoop。
- **Epoll**：对 Linux epoll 的封装。负责 epoll_create、epoll_ctl（添加/修改fd）、epoll_wait（等待就绪事件），返回活跃的 Channel 列表。
- **Channel**：一个fd的事件封装。记录该fd关心的事件（EPOLLIN等）、实际发生的事件、是否已注册到epoll，以及事件触发时的回调函数。它让epoll和业务逻辑解耦。
- **Acceptor**：监听Socket的管理者。绑定地址、listen，当有新连接到来时调用 accept 拿到客户端fd，然后回调通知 Server 创建 Connection。
- **Connection**：一个客户端连接的封装。持有该客户端的 Socket 和 Channel，实现HTTP请求解析和路由分发。GET请求在Reactor线程内联处理，POST请求（/register、/login）提交到DB线程池异步处理。
- **Socket**：对 socket fd 的 RAII 封装。提供 bind、listen、accept、connect、setnonblocking，析构时自动 close(fd)。
- **ThreadPool**：线程池。构造时创建N个工作线程，通过任务队列+条件变量调度。add() 接受任意可调用对象，返回 std::future 获取结果。
- **MysqlPool**：MySQL 连接池（单例模式）。内部用 `std::queue<MYSQL*>` + mutex + condition_variable 管理连接。配套 MysqlGuard（RAII）自动归还连接。
- **RedisPool**：Redis 连接池（单例模式，`#ifdef USE_REDIS`）。同样的队列+cv模式，提供 `get()`、`setex()`、`del()` 便捷方法。用于用户密码缓存和会话Token存储。
- **Buffer**：简单的读写缓冲区，内部用 std::vector\<char\> 存储，提供 append、clear、c_str 等操作。
- **InetAddress**：对 sockaddr_in 的封装，方便传递IP和端口。
- **util**：提供 errif() 函数，条件为真时打印错误并 exit(1)。

## 编译运行

### 依赖安装

```bash
sudo apt install libmysqlclient-dev libhiredis-dev
```

### 数据库准备

```sql
CREATE DATABASE IF NOT EXISTS myserver;
USE myserver;
CREATE TABLE users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(64) NOT NULL UNIQUE,
    password VARCHAR(128) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

数据库配置在 `src/MysqlPool.cpp`（默认 root/admin@127.0.0.1:3306），Redis 配置在 `src/RedisPool.cpp`（默认 127.0.0.1:6379）。

### 编译

```bash
cd build
cmake ..
make
```

### 运行

```bash
./server    # 启动HTTP服务器，监听 127.0.0.1:8888
```

## HTTP 接口

| 接口 | 方法 | 请求体 | 成功响应 | 错误响应 |
|------|------|--------|----------|----------|
| `/` | GET | — | 200 欢迎页HTML | — |
| `/hello` | GET | — | 200 Hello HTML | — |
| `/register` | POST | `{"username":"x","password":"y"}` | 200 `{"message":"register success"}` | 409 用户已存在 |
| `/login` | POST | `{"username":"x","password":"y"}` | 200 `{"message":"login success","token":"..."}` | 401 密码错误/用户不存在 |

### 测试

```bash
# GET 路由
curl http://127.0.0.1:8888/
curl http://127.0.0.1:8888/hello

# 注册
curl -X POST -d '{"username":"test","password":"123"}' http://127.0.0.1:8888/register

# 登录
curl -X POST -d '{"username":"test","password":"123"}' http://127.0.0.1:8888/login
```

### 压力测试

```bash
./test_stress [-c conns] [-m msgs] [-t threads] [-s ip] [-p port]
# 默认：10000连接，10条消息/连接，CPU核心数线程，127.0.0.1:8888
# 注意：压测客户端发送的是echo模式数据，不是HTTP请求
```
