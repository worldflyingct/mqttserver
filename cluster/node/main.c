#include <stdio.h>
#include "config.h"
#include "event_poll.h"
#include "ws.h"

int main () {
    InitConfig();
    if (event_poll_create()) {
        return -1;
    }
    if (Ws_Create()) {
        return -2;
    }
    event_poll_loop();
    return 0;
}
