#ifndef __EVENT_POLL_H___
#define __EVENT_POLL_H___

#include <sys/epoll.h>

typedef int EVENT_FUNCTION (int event, void *ptr);

struct EPOLL_PARAMS {
    int fd;
    EVENT_FUNCTION *func;
    void *ptr;
    struct EPOLL_PARAMS *tail;
};

int event_poll_create ();
void event_poll_loop ();
struct EPOLL_PARAMS *add_fd_to_poll (int fd, EVENT_FUNCTION func, void *ptr);
void remove_fd_from_poll (struct EPOLL_PARAMS *param);

#endif
