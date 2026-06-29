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
