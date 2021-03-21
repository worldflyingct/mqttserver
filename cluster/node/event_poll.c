#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include "event_poll.h"

#define MAXEVENTS      2048

struct EPOLL_PARAMS *remainparam = NULL;

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
        struct EPOLL_PARAMS *param = evs[i].data.ptr;
        param->func(evs[i].events, param->ptr);
    }
    goto LOOP;
}

struct EPOLL_PARAMS *add_fd_to_poll (int fd, EVENT_FUNCTION func, void *ptr) {
    struct EPOLL_PARAMS *param;
    if (remainparam) {
        param = remainparam;
        remainparam = remainparam->tail;
    } else {
        param = (struct EPOLL_PARAMS*)malloc(sizeof(struct EPOLL_PARAMS));
        if (param == NULL) {
            printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
            return NULL;
        }
    }
    param->fd = fd;
    param->func = func;
    param->ptr = ptr;
    struct epoll_event ev;
    ev.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLIN;
    ev.data.ptr = param;

    // 设置为非阻塞
    int fdflags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fdflags | O_NONBLOCK);

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev)) {
        printf("add fd:%d to poll, in %s, at %d\n", fd, __FILE__, __LINE__);
        param->tail = remainparam;
        remainparam = param;
        return NULL;
    }
    return param;
}

void remove_fd_from_poll (struct EPOLL_PARAMS *param) {
    struct epoll_event ev;
    param->tail = remainparam;
    remainparam = param;
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, param->fd, &ev)) {
        printf("remove fd:%d remove poll fail, in %s, at %d\n", param->fd, __FILE__, __LINE__);
    }
}
