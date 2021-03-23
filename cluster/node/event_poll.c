#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include "event_poll.h"

#define MAXEVENTS      2048

struct EPOLL {
    int fd;
    EVENT_FUNCTION *func;
};

static int epollfd = 0;
static int wait_count;
static struct epoll_event evs[MAXEVENTS];

int event_poll_create () {
    if ((epollfd = epoll_create(1024)) < 0) { // epoll_create的参数在高版本中已经废弃了，填入一个大于0的数字都一样。
        printf("epoll fd create fail, in %s, at %d\n", __FILE__, __LINE__);
        return -1;
    }
    return 0;
}

void event_poll_loop () {
LOOP:
    wait_count = epoll_wait(epollfd, evs, MAXEVENTS, -1);
    for (int i = 0 ; i < wait_count ; i++) {
        struct EPOLL *epoll = evs[i].data.ptr;
        epoll->func(evs[i].events);
    }
    goto LOOP;
}

int add_fd_to_poll (void *ptr) {
    struct EPOLL *epoll = ptr;
    struct epoll_event ev;
    ev.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLIN;
    ev.data.ptr = epoll;

    // 设置为非阻塞
    int fdflags = fcntl(epoll->fd, F_GETFL, 0);
    fcntl(epoll->fd, F_SETFL, fdflags | O_NONBLOCK);

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, epoll->fd, &ev)) {
        printf("add fd:%d to poll, in %s, at %d\n", epoll->fd, __FILE__, __LINE__);
        return -1;
    }
    return 0;
}

void remove_fd_from_poll (void *ptr) {
    struct EPOLL *epoll = ptr;
    struct epoll_event ev;
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, epoll->fd, &ev)) {
        printf("remove fd:%d remove poll fail, in %s, at %d\n", epoll->fd, __FILE__, __LINE__);
    }
    close(epoll->fd);
}
