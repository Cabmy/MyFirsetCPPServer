#include "src/Server.h"
#include "src/EventLoop.h"

int main()
{
    EventLoop *loop = new EventLoop();
    Server *serv = new Server(loop);
    loop->loop();

    return 0;
}