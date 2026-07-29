# MyFirstCPPServer

基于主从 Reactor 多线程模型的高并发 HTTP 服务器：epoll ET + 线程池 + MySQL/Redis 连接池，
支持用户注册/登录/会话鉴权，HTTP/1.1 keep-alive 长连接。

## 技术栈

- **语言/构建**：C++14 + CMake
- **网络**：Linux epoll 边缘触发(ET) + 非阻塞 I/O；主从 Reactor（1 主 Reactor accept + N 从 Reactor，N = CPU 核数）
- **连接**：HTTP/1.1 keep-alive（持久输入缓冲 + Content-Length 分帧）；写路径 EPOLLOUT + 应用层写缓冲；`GET /` 静态页走 `sendfile` 零拷贝
- **线程池**：`std::packaged_task` + `std::future`；DB 线程池隔离阻塞的 MySQL/Redis 调用
- **存储**：MySQL 连接池（RAII 归还）+ Redis 连接池（用户缓存 + 会话 Token），连接池获取超时降级 503
- **日志**：spdlog 异步日志（后台线程，业务线程不阻塞 IO）
- **健壮性**：空闲连接超时、请求头/体大小限制、全局限流、优雅关闭（SIGINT/SIGTERM）

## 架构

```
        ┌──────────────────────────┐
        │  Main Reactor            │  accept 新连接, fd % N 分发
        │  (EventLoop + Acceptor)  │
        └──────────┬───────────────┘
     ┌─────────────┼─────────────┐
     ▼             ▼             ▼
 Sub Reactor 0  Sub Reactor 1  Sub Reactor N     (thPool_, 每线程一个 EventLoop)
     │  GET 内联处理 / POST + /profile 提交 DB 线程池
     └─────────────┼─────────────┘
                   ▼
        DB ThreadPool (dbPool_) → MySQL 连接池 + Redis 连接池
```

核心组件：**Server**（调度器，持有 Reactor/线程池/连接表）、**EventLoop**（一个 Reactor 事件循环）、
**Epoll**/**Channel**（epoll 封装 + fd 事件解耦）、**Acceptor**（监听/accept）、**Connection**（HTTP 解析与路由）、
**ThreadPool**、**MysqlPool**/**RedisPool**（单例连接池）。所有可调参数集中在 `src/include/Config.h`。

## 编译运行

```bash
# 1. 依赖
sudo apt install libmysqlclient-dev libhiredis-dev libspdlog-dev

# 2. 数据库
mysql -e "CREATE DATABASE IF NOT EXISTS myserver;
USE myserver;
CREATE TABLE users(id INT AUTO_INCREMENT PRIMARY KEY,
  username VARCHAR(64) NOT NULL UNIQUE, password VARCHAR(128) NOT NULL)
  ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;"

# 3. 编译运行
cd build && cmake .. && make
./server        # 监听 127.0.0.1:8888
```

配置集中在 `src/include/Config.h`。地址/凭证可用 `MYSERVER_*` 环境变量覆盖（无需重编译）：
`MYSERVER_HOST/PORT`、`MYSERVER_DB_HOST/PORT/USER/PASSWD/NAME`、`MYSERVER_REDIS_HOST/PORT`、
`MYSERVER_STATIC_DIR`、`MYSERVER_LOG_FILE`；连接限值（超时/大小/限流）为 Config.h 编译期常量。

> WSL NAT 提示：MySQL 在 Windows 宿主机时用 `MYSERVER_DB_HOST=$(ip route show default | awk '{print $3}')`；
> Redis protected mode 拒绝 IPv4 回环时用 `MYSERVER_REDIS_HOST=::1`。
