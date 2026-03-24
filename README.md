# MyFirsetCPPServer

# Echo服务器工作流程

## 技术栈

- 语言：C++14
- I/O多路复用：Linux epoll（边缘触发 ET 模式 + 非阻塞 I/O）
- 并发模型：主从 Reactor 多线程模式（1个主Reactor接收连接 + N个从Reactor处理业务，N = CPU核心数）
- 线程池：基于 std::packaged_task + std::future，支持任意可调用对象和返回值获取
- 内存管理：std::unique_ptr 管理 Socket、Channel、Connection 等对象的生命周期（RAII）
- 构建工具：CMake

## 各类的作用

- **Server**：总调度器。持有主Reactor、从Reactor数组、线程池、连接表（fd → Connection）。负责接收新连接并分配到从Reactor，以及删除断开的连接。
- **EventLoop**：事件循环，即一个Reactor。内部持有一个Epoll，不断调用 epoll_wait 获取就绪事件，然后逐个调用 Channel 的回调。每个线程跑一个EventLoop。
- **Epoll**：对 Linux epoll 的封装。负责 epoll_create、epoll_ctl（添加/修改fd）、epoll_wait（等待就绪事件），返回活跃的 Channel 列表。
- **Channel**：一个fd的事件封装。记录该fd关心的事件（EPOLLIN等）、实际发生的事件、是否已注册到epoll，以及事件触发时的回调函数。它让epoll和业务逻辑解耦。
- **Acceptor**：监听Socket的管理者。绑定地址、listen，当有新连接到来时调用 accept 拿到客户端fd，然后回调通知 Server 创建 Connection。
- **Connection**：一个客户端连接的封装。持有该客户端的 Socket 和 Channel，注册读事件回调（echo），负责读取数据并原样写回。
- **Socket**：对 socket fd 的 RAII 封装。提供 bind、listen、accept、connect、setnonblocking，析构时自动 close(fd)。
- **ThreadPool**：线程池。构造时创建N个工作线程，通过任务队列+条件变量调度。add() 接受任意可调用对象，返回 std::future 获取结果。
- **Buffer**：简单的读写缓冲区，内部用 std::vector<char> 存储，提供 append、clear、c_str 等操作。
- **InetAddress**：对 sockaddr_in 的封装，方便传递IP和端口。
- **util**：提供 errif() 函数，条件为真时打印错误并 exit(1)。

# 改进：Echo → HTTP 服务器

## 改动内容

只修改了 Connection 类，其他所有类（Server、EventLoop、Epoll、Channel、Acceptor、Socket、ThreadPool、Buffer）完全不变。