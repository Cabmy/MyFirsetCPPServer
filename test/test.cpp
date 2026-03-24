#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// ==================== 数据结构 ====================

enum class Phase { CONNECTING, WRITING, READING, DONE, FAILED };

struct ConnState {
    int fd = -1;
    Phase phase = Phase::CONNECTING;
    int msgs_sent = 0;
    int msgs_target = 0;
    int bytes_written = 0;
    int bytes_read = 0;
    int bytes_expected = 0;
    char send_buf[128];
    char read_buf[128];
    std::chrono::steady_clock::time_point send_time;
    std::vector<double> latencies_us;
};

struct WorkerStats {
    int connected_ok = 0;
    int connect_fail = 0;
    int messages_ok = 0;
    int verify_fail = 0;
    std::vector<double> latencies_us;
};

// ==================== 辅助函数 ====================

static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void epoll_mod(int epfd, int fd, uint32_t events, uint32_t data)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.u32 = data;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

// ==================== 事件处理 ====================

static void handle_connect(int epfd, ConnState &cs, uint32_t idx, WorkerStats &stats)
{
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(cs.fd, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) {
        cs.phase = Phase::FAILED;
        close(cs.fd);
        cs.fd = -1;
        stats.connect_fail++;
        return;
    }
    stats.connected_ok++;
    // 准备第一条消息
    cs.msgs_sent = 0;
    cs.bytes_expected = snprintf(cs.send_buf, sizeof(cs.send_buf),
                                  "hello from conn %u", idx);
    cs.bytes_written = 0;
    cs.send_time = std::chrono::steady_clock::now();
    cs.phase = Phase::WRITING;
    epoll_mod(epfd, cs.fd, EPOLLOUT | EPOLLET, idx);
}

static void handle_write(int epfd, ConnState &cs, uint32_t idx, WorkerStats &stats)
{
    while (cs.bytes_written < cs.bytes_expected) {
        ssize_t n = write(cs.fd, cs.send_buf + cs.bytes_written,
                          cs.bytes_expected - cs.bytes_written);
        if (n > 0) {
            cs.bytes_written += n;
        } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return; // 等下次 EPOLLOUT
        } else {
            cs.phase = Phase::FAILED;
            close(cs.fd);
            cs.fd = -1;
            return;
        }
    }
    // 写完，切换到读
    cs.bytes_read = 0;
    cs.phase = Phase::READING;
    epoll_mod(epfd, cs.fd, EPOLLIN | EPOLLET, idx);
}

static void handle_read(int epfd, ConnState &cs, uint32_t idx, WorkerStats &stats)
{
    while (cs.bytes_read < cs.bytes_expected) {
        ssize_t n = read(cs.fd, cs.read_buf + cs.bytes_read,
                         cs.bytes_expected - cs.bytes_read);
        if (n > 0) {
            cs.bytes_read += n;
        } else if (n == 0) {
            // 服务端关闭
            cs.phase = Phase::FAILED;
            close(cs.fd);
            cs.fd = -1;
            return;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return; // 等下次 EPOLLIN
        } else {
            cs.phase = Phase::FAILED;
            close(cs.fd);
            cs.fd = -1;
            return;
        }
    }

    // 读完一条消息，记录延迟
    auto now = std::chrono::steady_clock::now();
    double lat = std::chrono::duration<double, std::micro>(now - cs.send_time).count();
    cs.latencies_us.push_back(lat);

    // 验证 echo
    if (memcmp(cs.send_buf, cs.read_buf, cs.bytes_expected) != 0) {
        stats.verify_fail++;
    }
    stats.messages_ok++;

    cs.msgs_sent++;
    if (cs.msgs_sent >= cs.msgs_target) {
        // 全部完成
        cs.phase = Phase::DONE;
        epoll_ctl(epfd, EPOLL_CTL_DEL, cs.fd, nullptr);
        close(cs.fd);
        cs.fd = -1;
    } else {
        // 发下一条消息
        cs.bytes_written = 0;
        cs.send_time = std::chrono::steady_clock::now();
        cs.phase = Phase::WRITING;
        epoll_mod(epfd, cs.fd, EPOLLOUT | EPOLLET, idx);
    }
}

// ==================== Worker 线程 ====================

static void worker_run(int thread_id, int conn_count, int msgs_per_conn,
                        const char *server_ip, uint16_t port,
                        WorkerStats &stats)
{
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        return;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &serv_addr.sin_addr);

    std::vector<ConnState> conns(conn_count);

    // 分批建立连接
    const int BATCH = 100;
    int active = 0;

    for (int i = 0; i < conn_count; i += BATCH) {
        int end = std::min(i + BATCH, conn_count);
        for (int j = i; j < end; ++j) {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                conns[j].phase = Phase::FAILED;
                stats.connect_fail++;
                continue;
            }
            set_nonblocking(fd);

            conns[j].fd = fd;
            conns[j].msgs_target = msgs_per_conn;

            int ret = ::connect(fd, (sockaddr *)&serv_addr, sizeof(serv_addr));

            struct epoll_event ev;
            ev.data.u32 = j;

            if (ret == 0) {
                // 立即连接成功（本地回环常见）
                stats.connected_ok++;
                conns[j].bytes_expected = snprintf(conns[j].send_buf,
                    sizeof(conns[j].send_buf), "hello from conn %d-%d", thread_id, j);
                conns[j].bytes_written = 0;
                conns[j].send_time = std::chrono::steady_clock::now();
                conns[j].phase = Phase::WRITING;
                ev.events = EPOLLOUT | EPOLLET;
            } else if (errno == EINPROGRESS) {
                conns[j].phase = Phase::CONNECTING;
                conns[j].bytes_expected = snprintf(conns[j].send_buf,
                    sizeof(conns[j].send_buf), "hello from conn %d-%d", thread_id, j);
                ev.events = EPOLLOUT | EPOLLET;
            } else {
                conns[j].phase = Phase::FAILED;
                close(fd);
                conns[j].fd = -1;
                stats.connect_fail++;
                continue;
            }

            epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
            active++;
        }
        if (i + BATCH < conn_count) {
            usleep(1000); // 批次间 1ms 延迟
        }
    }

    // 事件循环
    const int MAX_EVENTS = 1024;
    struct epoll_event events[MAX_EVENTS];
    int timeout_count = 0;

    while (active > 0) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, 1000);
        if (n == 0) {
            timeout_count++;
            if (timeout_count >= 30) {
                // 超时 30 秒，放弃剩余连接
                fprintf(stderr, "[thread %d] timeout, %d connections still active\n",
                        thread_id, active);
                break;
            }
            continue;
        }
        timeout_count = 0;

        for (int i = 0; i < n; ++i) {
            uint32_t idx = events[i].data.u32;
            ConnState &cs = conns[idx];
            uint32_t ev = events[i].events;

            if (ev & (EPOLLERR | EPOLLHUP)) {
                if (cs.phase == Phase::CONNECTING) {
                    stats.connect_fail++;
                } else {
                    // 已连接后断开
                }
                cs.phase = Phase::FAILED;
                epoll_ctl(epfd, EPOLL_CTL_DEL, cs.fd, nullptr);
                close(cs.fd);
                cs.fd = -1;
                active--;
                continue;
            }

            Phase before = cs.phase;

            switch (cs.phase) {
            case Phase::CONNECTING:
                handle_connect(epfd, cs, idx, stats);
                // connect 成功后可能直接可写，尝试写
                if (cs.phase == Phase::WRITING) {
                    handle_write(epfd, cs, idx, stats);
                }
                break;
            case Phase::WRITING:
                handle_write(epfd, cs, idx, stats);
                break;
            case Phase::READING:
                handle_read(epfd, cs, idx, stats);
                break;
            default:
                break;
            }

            if (cs.phase == Phase::DONE || cs.phase == Phase::FAILED) {
                if (before != Phase::FAILED && before != Phase::DONE) {
                    active--;
                }
            }
        }
    }

    // 清理未完成的连接
    for (auto &cs : conns) {
        if (cs.fd >= 0) {
            close(cs.fd);
        }
        // 收集延迟数据
        stats.latencies_us.insert(stats.latencies_us.end(),
                                   cs.latencies_us.begin(),
                                   cs.latencies_us.end());
    }

    close(epfd);
}

// ==================== 统计输出 ====================

static void print_stats(const std::vector<WorkerStats> &all_stats,
                         double duration_sec, int total_conns)
{
    int connected = 0, failed = 0, msgs = 0, verify_fail = 0;
    std::vector<double> all_lat;

    for (auto &s : all_stats) {
        connected += s.connected_ok;
        failed += s.connect_fail;
        msgs += s.messages_ok;
        verify_fail += s.verify_fail;
        all_lat.insert(all_lat.end(), s.latencies_us.begin(), s.latencies_us.end());
    }

    printf("\n============ C10K Stress Test Results ============\n");
    printf("Connections:  %d attempted, %d connected, %d failed\n",
           total_conns, connected, failed);
    printf("Messages:     %d echoed OK, %d verify failures\n",
           msgs, verify_fail);

    if (!all_lat.empty()) {
        std::sort(all_lat.begin(), all_lat.end());
        double sum = std::accumulate(all_lat.begin(), all_lat.end(), 0.0);
        size_t n = all_lat.size();
        printf("Latency (us): avg=%.0f, min=%.0f, max=%.0f, p50=%.0f, p99=%.0f\n",
               sum / n,
               all_lat.front(),
               all_lat.back(),
               all_lat[n * 50 / 100],
               all_lat[n * 99 / 100]);
    }

    printf("Throughput:   %.0f msg/sec\n", msgs / duration_sec);
    printf("Duration:     %.2f seconds\n", duration_sec);
    printf("==================================================\n");
}

// ==================== main ====================

int main(int argc, char *argv[])
{
    int total_conns = 10000;
    int msgs = 10;
    int threads = std::thread::hardware_concurrency();
    const char *server_ip = "127.0.0.1";
    int port = 8888;

    int o;
    const char *optstring = "c:m:t:s:p:";
    while ((o = getopt(argc, argv, optstring)) != -1) {
        switch (o) {
        case 'c': total_conns = atoi(optarg); break;
        case 'm': msgs = atoi(optarg); break;
        case 't': threads = atoi(optarg); break;
        case 's': server_ip = optarg; break;
        case 'p': port = atoi(optarg); break;
        default:
            fprintf(stderr, "Usage: %s [-c conns] [-m msgs] [-t threads] [-s ip] [-p port]\n",
                    argv[0]);
            return 1;
        }
    }

    printf("C10K Stress Test: %d connections, %d msgs/conn, %d threads\n",
           total_conns, msgs, threads);
    printf("Target: %s:%d\n\n", server_ip, port);

    std::vector<WorkerStats> all_stats(threads);
    std::vector<std::thread> workers;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < threads; ++i) {
        int conns_for_thread = total_conns / threads;
        // 前几个线程多分一个，处理不能整除的情况
        if (i < total_conns % threads) {
            conns_for_thread++;
        }
        workers.emplace_back(worker_run, i, conns_for_thread, msgs,
                             server_ip, (uint16_t)port,
                             std::ref(all_stats[i]));
    }

    for (auto &t : workers) {
        t.join();
    }

    auto end = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double>(end - start).count();

    print_stats(all_stats, duration, total_conns);

    return 0;
}
