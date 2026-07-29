#include "Server.h"
#include "EventLoop.h"
#include "Config.h"
#include "Log.h"

#include <spdlog/spdlog.h>
#include <csignal>

// Signal handler only sets an async-signal-safe flag; the main reactor's
// per-iteration callback (below) acts on it on a normal thread.
static volatile sig_atomic_t g_shutdown = 0;

static void onSignal(int)
{
    g_shutdown = 1; // SIGINT / SIGTERM
}

int main()
{
    // Print the effective configuration at startup.
    LOG_INFO("MyFirstCPPServer starting");
    LOG_INFO("HTTP listen : %s:%d", config::serverHost().c_str(), config::serverPort());
    LOG_INFO("MySQL       : %s:%u db=%s user=%s",
             config::dbHost().c_str(), config::dbPort(),
             config::dbName().c_str(), config::dbUser().c_str());
    LOG_INFO("Redis       : %s:%u", config::redisHost().c_str(), config::redisPort());
    LOG_INFO("static dir  : %s", config::staticDir().c_str());

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    signal(SIGHUP, SIG_IGN);  // hot-reload removed; ignore rather than let it kill us
    signal(SIGPIPE, SIG_IGN); // writing to a peer-closed socket must not kill us

    EventLoop *loop = new EventLoop();
    Server *serv = new Server(loop);

    // Runs once per main-reactor poll iteration (≤1s): honor shutdown + sweep idle.
    loop->setLoopCallback([&]()
                          {
        if (g_shutdown)
        {
            LOG_INFO("shutdown signal received, stopping reactors");
            serv->stop();
            return;
        }
        serv->sweepIdle(); });

    loop->loop(); // blocks until a shutdown signal sets quit_

    LOG_INFO("event loop exited, cleaning up");
    delete serv; // ThreadPools stop+join, connections closed, pools freed
    delete loop;
    spdlog::shutdown(); // flush the async logger
    return 0;
}
