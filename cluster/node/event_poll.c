#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include "event_poll.h"

#define MAXEVENTS      2048

static unsigned char buff[512*1024];
static EPOLL *remainepoll = NULL;

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
        EPOLL *epoll = evs[i].data.ptr;
        epoll->func(evs[i].events, epoll, buff);
    }
    goto LOOP;
}

EPOLL *add_fd_to_poll (int fd, int eout) {
    // 设置为非阻塞
    int fdflags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fdflags | O_NONBLOCK);

    EPOLL *epoll;
    if (remainepoll) {
        epoll = remainepoll;
        remainepoll = remainepoll->tail;
    } else {
        epoll = (EPOLL*)malloc(sizeof(EPOLL));
        if (epoll == NULL) {
            printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
            return NULL;
        }
    }
    epoll->fd = fd;
    struct epoll_event ev;
    if (eout) {
        ev.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLIN | EPOLLOUT;
    } else {
        ev.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLIN;
    }
    ev.data.ptr = epoll;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev)) {
        printf("add fd:%d to poll, in %s, at %d\n", fd, __FILE__, __LINE__);
        epoll->tail = remainepoll;
        remainepoll = epoll;
        return NULL;
    }
    return epoll;
}

int mod_fd_at_poll (EPOLL *epoll, int eout) {
    struct epoll_event ev;
    if (eout) {
        ev.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLIN | EPOLLOUT;
    } else {
        ev.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLIN;
    }
    ev.data.ptr = epoll;
    int fd = epoll->fd;
    if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev)) {
        printf("modify fd:%d to poll, in %s, at %d\n", fd, __FILE__, __LINE__);
        return -1;
    }
    return 0;
}

void remove_fd_from_poll (EPOLL *epoll) {
    struct epoll_event ev;
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, epoll->fd, &ev)) {
        printf("remove fd:%d remove poll fail, in %s, at %d\n", epoll->fd, __FILE__, __LINE__);
        epoll->tail = remainepoll;
        remainepoll = epoll;
    }
    close(epoll->fd);
}
