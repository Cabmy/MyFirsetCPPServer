// HTTP 短连接压测客户端
// 架构: 多 worker 线程, 每线程独立 epoll-ET 实例, 状态机驱动
// 协议: HTTP/1.1 GET /, 服务器恒发 Connection: close, 故每条 msg 后需重连
//
// 命令行: -c slots -m msgs/slot -t threads -s ip -p port
//   slots:     总并发槽位 (类似 wrk 的 -c)
//   msgs/slot: 每个槽位完成多少次完整请求-响应循环
//   总请求数 = slots * msgs

#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// ==================== 状态机 ====================

enum class Phase {
    CONNECTING,
    WRITING,
    READING_HEADERS,
    READING_BODY,
    DONE,
    FAILED
};

struct ConnState {
    int fd = -1;
    Phase phase = Phase::CONNECTING;
    int msgs_done = 0;
    int msgs_target = 0;

    // 写
    std::string request;
    size_t bytes_written = 0;

    // 读
    std::string response;
    int header_end = -1;       // \r\n\r\n 的位置
    int content_length = -1;
    int total_expected = -1;   // header_end + 4 + content_length

    std::chrono::steady_clock::time_point send_time;
    std::vector<double> latencies_us;
};

struct WorkerStats {
    int connected_ok = 0;
    int connect_fail = 0;
    int requests_ok = 0;       // 完整收到响应的请求数 (含非200)
    int verify_fail = 0;       // 响应不是 HTTP/1.1 200 的次数
    int request_timeout = 0;   // 单请求超过 PER_REQ_TIMEOUT_SEC 未收到响应
    std::vector<double> latencies_us;
};

// 单个请求 (从 connect 开始计时) 超过此秒数视为超时, 放弃当前 slot.
// 避免服务端 bug 导致 stuck slot 拖垮整个测试.
static const double PER_REQ_TIMEOUT_SEC = 5.0;

// ==================== 辅助函数 ====================

static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void epoll_add(int epfd, int fd, uint32_t events, uint32_t data)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.u32 = data;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

static void epoll_mod(int epfd, int fd, uint32_t events, uint32_t data)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.u32 = data;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

// 解析 HTTP 响应 header. 成功后填充 cs.header_end / content_length / total_expected.
// 返回 true 表示 header 已完整接收.
static bool parse_http_headers(ConnState &cs)
{
    size_t pos = cs.response.find("\r\n\r\n");
    if (pos == std::string::npos) {
        return false;
    }
    cs.header_end = static_cast<int>(pos);

    // 仅在 header 范围内查找 Content-Length
    size_t cl = cs.response.find("Content-Length:");
    if (cl == std::string::npos || cl >= pos) {
        cs.content_length = 0;
    } else {
        size_t v = cl + sizeof("Content-Length:") - 1;
        while (v < pos && (cs.response[v] == ' ' || cs.response[v] == '\t')) v++;
        cs.content_length = std::atoi(cs.response.c_str() + v);
    }
    cs.total_expected = cs.header_end + 4 + cs.content_length;
    return true;
}

// ==================== 重连 (短连接所需) ====================

static bool reconnect(int epfd, ConnState &cs, uint32_t idx,
                      const sockaddr_in &serv_addr, WorkerStats &stats)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        cs.phase = Phase::FAILED;
        stats.connect_fail++;
        return false;
    }
    set_nonblocking(fd);

    cs.fd = fd;
    cs.bytes_written = 0;
    cs.response.clear();
    cs.header_end = -1;
    cs.content_length = -1;
    cs.total_expected = -1;

    int ret = ::connect(fd, (sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret == 0) {
        stats.connected_ok++;
        cs.send_time = std::chrono::steady_clock::now();
        cs.phase = Phase::WRITING;
        epoll_add(epfd, fd, EPOLLOUT | EPOLLET, idx);
    } else if (errno == EINPROGRESS) {
        cs.phase = Phase::CONNECTING;
        cs.send_time = std::chrono::steady_clock::now();
        epoll_add(epfd, fd, EPOLLOUT | EPOLLET, idx);
    } else {
        close(fd);
        cs.fd = -1;
        cs.phase = Phase::FAILED;
        stats.connect_fail++;
        return false;
    }
    return true;
}

// 完成一次 HTTP 请求-响应循环, 计算延迟, 决定重连或结束
static void finish_one_request(int epfd, ConnState &cs, uint32_t idx,
                                const sockaddr_in &serv_addr, WorkerStats &stats)
{
    auto now = std::chrono::steady_clock::now();
    double lat = std::chrono::duration<double, std::micro>(now - cs.send_time).count();
    cs.latencies_us.push_back(lat);
    stats.requests_ok++;

    // 校验状态码
    if (cs.response.size() < 12 || cs.response.compare(0, 12, "HTTP/1.1 200") != 0) {
        stats.verify_fail++;
    }

    cs.msgs_done++;

    // 关闭当前 fd (server 发了 Connection: close)
    if (cs.fd >= 0) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, cs.fd, nullptr);
        close(cs.fd);
        cs.fd = -1;
    }

    if (cs.msgs_done >= cs.msgs_target) {
        cs.phase = Phase::DONE;
    } else {
        reconnect(epfd, cs, idx, serv_addr, stats);
    }
}

// ==================== 事件处理 ====================

static void handle_connect(int epfd, ConnState &cs, uint32_t idx, WorkerStats &stats)
{
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(cs.fd, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, cs.fd, nullptr);
        close(cs.fd);
        cs.fd = -1;
        cs.phase = Phase::FAILED;
        stats.connect_fail++;
        return;
    }
    stats.connected_ok++;
    cs.send_time = std::chrono::steady_clock::now();
    cs.phase = Phase::WRITING;
    epoll_mod(epfd, cs.fd, EPOLLOUT | EPOLLET, idx);
}

static void handle_write(int epfd, ConnState &cs, uint32_t idx, WorkerStats &stats)
{
    while (cs.bytes_written < cs.request.size()) {
        ssize_t n = write(cs.fd, cs.request.data() + cs.bytes_written,
                          cs.request.size() - cs.bytes_written);
        if (n > 0) {
            cs.bytes_written += n;
        } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        } else if (n == -1 && errno == EINTR) {
            continue;
        } else {
            epoll_ctl(epfd, EPOLL_CTL_DEL, cs.fd, nullptr);
            close(cs.fd);
            cs.fd = -1;
            cs.phase = Phase::FAILED;
            return;
        }
    }
    cs.phase = Phase::READING_HEADERS;
    epoll_mod(epfd, cs.fd, EPOLLIN | EPOLLET, idx);
}

static void handle_read(int epfd, ConnState &cs, uint32_t idx,
                        const sockaddr_in &serv_addr, WorkerStats &stats)
{
    char buf[4096];
    bool peer_closed = false;

    while (true) {
        ssize_t n = read(cs.fd, buf, sizeof(buf));
        if (n > 0) {
            cs.response.append(buf, n);
        } else if (n == 0) {
            peer_closed = true;
            break;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            epoll_ctl(epfd, EPOLL_CTL_DEL, cs.fd, nullptr);
            close(cs.fd);
            cs.fd = -1;
            cs.phase = Phase::FAILED;
            return;
        }
    }

    // 阶段一: 找 \r\n\r\n
    if (cs.phase == Phase::READING_HEADERS) {
        if (parse_http_headers(cs)) {
            cs.phase = Phase::READING_BODY;
        }
    }

    // 阶段二: 等 body 读够
    if (cs.phase == Phase::READING_BODY) {
        if (cs.total_expected >= 0 &&
            static_cast<int>(cs.response.size()) >= cs.total_expected) {
            finish_one_request(epfd, cs, idx, serv_addr, stats);
            return;
        }
    }

    // 服务端关闭但响应不完整 -> 失败
    if (peer_closed) {
        epoll_ctl(epfd, EPOLL_CTL_DEL, cs.fd, nullptr);
        close(cs.fd);
        cs.fd = -1;
        cs.phase = Phase::FAILED;
    }
}

// ==================== Worker ====================

static void worker_run(int thread_id, int slot_count, int msgs_per_slot,
                        const char *server_ip, uint16_t port,
                        WorkerStats &stats)
{
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        return;
    }

    sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &serv_addr.sin_addr);

    // 预生成 HTTP 请求文本
    char req_buf[256];
    int rlen = snprintf(req_buf, sizeof(req_buf),
                        "GET / HTTP/1.1\r\n"
                        "Host: %s:%u\r\n"
                        "User-Agent: stress-test\r\n"
                        "\r\n",
                        server_ip, port);
    std::string http_request(req_buf, rlen);

    std::vector<ConnState> conns(slot_count);
    int active = 0;

    // 分批建连, 避免 SYN 洪泛
    const int BATCH = 100;
    for (int i = 0; i < slot_count; i += BATCH) {
        int end = std::min(i + BATCH, slot_count);
        for (int j = i; j < end; ++j) {
            conns[j].msgs_target = msgs_per_slot;
            conns[j].request = http_request;

            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                conns[j].phase = Phase::FAILED;
                stats.connect_fail++;
                continue;
            }
            set_nonblocking(fd);
            conns[j].fd = fd;

            int ret = ::connect(fd, (sockaddr *)&serv_addr, sizeof(serv_addr));
            if (ret == 0) {
                stats.connected_ok++;
                conns[j].send_time = std::chrono::steady_clock::now();
                conns[j].phase = Phase::WRITING;
                epoll_add(epfd, fd, EPOLLOUT | EPOLLET, j);
            } else if (errno == EINPROGRESS) {
                conns[j].phase = Phase::CONNECTING;
                conns[j].send_time = std::chrono::steady_clock::now();
                epoll_add(epfd, fd, EPOLLOUT | EPOLLET, j);
            } else {
                close(fd);
                conns[j].fd = -1;
                conns[j].phase = Phase::FAILED;
                stats.connect_fail++;
                continue;
            }
            active++;
        }
        if (i + BATCH < slot_count) usleep(1000);
    }

    // 事件循环
    const int MAX_EVENTS = 1024;
    epoll_event events[MAX_EVENTS];
    int idle_ticks = 0;

    while (active > 0) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, 500);
        if (n == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        // 扫描每个 slot, 标记超时的为 FAILED
        // (服务端 bug 可能让某些 slot 永不收到响应)
        auto now_ts = std::chrono::steady_clock::now();
        for (size_t k = 0; k < conns.size(); ++k) {
            ConnState &cs = conns[k];
            if (cs.phase == Phase::DONE || cs.phase == Phase::FAILED) continue;
            double elapsed = std::chrono::duration<double>(now_ts - cs.send_time).count();
            if (elapsed > PER_REQ_TIMEOUT_SEC) {
                if (cs.fd >= 0) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, cs.fd, nullptr);
                    close(cs.fd);
                    cs.fd = -1;
                }
                cs.phase = Phase::FAILED;
                stats.request_timeout++;
                active--;
            }
        }
        if (active <= 0) break;

        if (n == 0) {
            if (++idle_ticks >= 60) {
                fprintf(stderr, "[thread %d] global idle, %d slots still active\n",
                        thread_id, active);
                break;
            }
            continue;
        }
        idle_ticks = 0;

        for (int i = 0; i < n; ++i) {
            uint32_t idx = events[i].data.u32;
            ConnState &cs = conns[idx];
            uint32_t ev = events[i].events;

            Phase before = cs.phase;

            if (ev & (EPOLLERR | EPOLLHUP)) {
                if (cs.phase == Phase::CONNECTING) {
                    stats.connect_fail++;
                }
                if (cs.fd >= 0) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, cs.fd, nullptr);
                    close(cs.fd);
                    cs.fd = -1;
                }
                cs.phase = Phase::FAILED;
            } else {
                switch (cs.phase) {
                case Phase::CONNECTING:
                    handle_connect(epfd, cs, idx, stats);
                    if (cs.phase == Phase::WRITING) {
                        handle_write(epfd, cs, idx, stats);
                    }
                    break;
                case Phase::WRITING:
                    handle_write(epfd, cs, idx, stats);
                    break;
                case Phase::READING_HEADERS:
                case Phase::READING_BODY:
                    handle_read(epfd, cs, idx, serv_addr, stats);
                    break;
                default:
                    break;
                }
            }

            // active 计数: 只在状态从非终态转入终态时减
            bool was_terminal = (before == Phase::DONE || before == Phase::FAILED);
            bool now_terminal = (cs.phase == Phase::DONE || cs.phase == Phase::FAILED);
            if (now_terminal && !was_terminal) {
                active--;
            }
        }
    }

    // 收尾
    for (auto &cs : conns) {
        if (cs.fd >= 0) close(cs.fd);
        stats.latencies_us.insert(stats.latencies_us.end(),
                                   cs.latencies_us.begin(),
                                   cs.latencies_us.end());
    }
    close(epfd);
}

// ==================== 统计输出 ====================

static void print_stats(const std::vector<WorkerStats> &all_stats,
                         double duration_sec, int total_slots, int msgs_per_slot)
{
    int connected = 0, conn_fail = 0, ok = 0, verify_fail = 0;
    std::vector<double> all_lat;
    for (auto &s : all_stats) {
        connected += s.connected_ok;
        conn_fail += s.connect_fail;
        ok += s.requests_ok;
        verify_fail += s.verify_fail;
        all_lat.insert(all_lat.end(), s.latencies_us.begin(), s.latencies_us.end());
    }

    int target = total_slots * msgs_per_slot;
    printf("\n========== HTTP Stress Test Results ==========\n");
    printf("Slots:        %d (%d msgs/slot, target = %d requests)\n",
           total_slots, msgs_per_slot, target);
    printf("Connections:  %d connect_ok, %d connect_fail (含重连)\n",
           connected, conn_fail);
    printf("Requests:     %d completed, %d non-200 responses\n",
           ok, verify_fail);
    int timeout_total = 0;
    for (auto &s : all_stats) timeout_total += s.request_timeout;
    if (timeout_total > 0) {
        printf("Timeouts:     %d slots stuck > %.1fs (server bug 嫌疑)\n",
               timeout_total, PER_REQ_TIMEOUT_SEC);
    }

    if (!all_lat.empty()) {
        std::sort(all_lat.begin(), all_lat.end());
        double sum = std::accumulate(all_lat.begin(), all_lat.end(), 0.0);
        size_t n = all_lat.size();
        printf("Latency (us): avg=%.0f  min=%.0f  p50=%.0f  p90=%.0f  p99=%.0f  max=%.0f\n",
               sum / n,
               all_lat.front(),
               all_lat[n * 50 / 100],
               all_lat[n * 90 / 100],
               all_lat[n * 99 / 100],
               all_lat.back());
    }

    printf("Requests/sec: %.0f\n", ok / duration_sec);
    printf("Duration:     %.2f seconds\n", duration_sec);
    printf("==============================================\n");
}

// ==================== main ====================

int main(int argc, char *argv[])
{
    int total_slots = 10000;
    int msgs = 10;
    int threads = std::thread::hardware_concurrency();
    const char *server_ip = "127.0.0.1";
    int port = 8888;

    int o;
    const char *optstring = "c:m:t:s:p:";
    while ((o = getopt(argc, argv, optstring)) != -1) {
        switch (o) {
        case 'c': total_slots = atoi(optarg); break;
        case 'm': msgs = atoi(optarg); break;
        case 't': threads = atoi(optarg); break;
        case 's': server_ip = optarg; break;
        case 'p': port = atoi(optarg); break;
        default:
            fprintf(stderr, "Usage: %s [-c slots] [-m msgs/slot] [-t threads] [-s ip] [-p port]\n",
                    argv[0]);
            return 1;
        }
    }

    printf("HTTP Stress Test: %d slots x %d msgs (short conn), %d threads\n",
           total_slots, msgs, threads);
    printf("Target: GET http://%s:%d/\n\n", server_ip, port);

    std::vector<WorkerStats> all_stats(threads);
    std::vector<std::thread> workers;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < threads; ++i) {
        int slots = total_slots / threads;
        if (i < total_slots % threads) slots++;
        workers.emplace_back(worker_run, i, slots, msgs,
                             server_ip, (uint16_t)port,
                             std::ref(all_stats[i]));
    }
    for (auto &t : workers) t.join();

    auto end = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double>(end - start).count();

    print_stats(all_stats, duration, total_slots, msgs);
    return 0;
}
