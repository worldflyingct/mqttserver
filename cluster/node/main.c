#include <stdio.h>
#include "config.h"
#include "event_poll.h"
#include "ws.h"
#include "tcp.h"

int main () {
    if (!InitConfig()) {
        return -1;
    }
    if (event_poll_create()) {
        return -2;
    }
    if (Tcp_Create()) {
        return -3;
    }
    if (Ws_Create()) {
        return -4;
    }
    event_poll_loop();
    return 0;
}
