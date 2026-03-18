#include "src/Server.h"
#include "src/EventLoop.h"

int main()
{
    EventLoop *loop = new EventLoop();
    Server *serv = new Server(loop);
    loop->loop();

    delete loop;
    delete serv;
    return 0;
}