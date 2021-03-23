#include <stdio.h>
#include "config.h"
#include "event_poll.h"
#include "ws.h"

unsigned char buff[512*1024];

int main () {
    InitConfig();
    if (event_poll_create()) {
        return -1;
    }
    if (Tcp_Create()) {
        return -2;
    }
    if (Ws_Create()) {
        return -3;
    }
    event_poll_loop();
    return 0;
}
