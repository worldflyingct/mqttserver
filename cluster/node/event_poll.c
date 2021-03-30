#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include "event_poll.h"

#define MAXEVENTS      2048

static unsigned char buffer[512*1024];

static int epollfd = 0;
static int wait_count;
static struct epoll_event evs[MAXEVENTS];

EPOLL *add_fd_to_poll (int fd, int eout) {
    // 设置为非阻塞
    int fdflags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fdflags | O_NONBLOCK);

    EPOLL *epoll = (EPOLL*)malloc(sizeof(EPOLL));
    if (epoll == NULL) {
        printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
        return NULL;
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
        free(epoll);
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

void Epoll_Delete (EPOLL *epoll) {
    struct epoll_event ev;
    epoll_ctl(epollfd, EPOLL_CTL_DEL, epoll->fd, &ev);
    close(epoll->fd);
    if (epoll->bufflen) {
        free(epoll->buff);
    }
    if (epoll->mqttpackagelen) {
        free(epoll->mqttpackage);
    }
    if (epoll->wspackagelen) {
        free(epoll->wspackage);
    }
    free(epoll);
}

static int Epoll_Event (int event, EPOLL *epoll) {
    if (event & (EPOLLERR|EPOLLHUP|EPOLLRDHUP)) { // 错误异常处理
        epoll->delete(epoll);
    } else if (event & EPOLLOUT) {
        if (epoll->bufflen > 0) {
            ssize_t res = write(epoll->fd, epoll->buff, epoll->bufflen);
            if (res == epoll->bufflen) {
                free(epoll->buff);
                epoll->buff = NULL;
                epoll->bufflen = 0;
                mod_fd_at_poll(epoll, 0);
                epoll->writeenable = 1;
            } else if ( 0 < res && res < epoll->bufflen) {
                unsigned long bufflen = epoll->bufflen - res;
                unsigned char *buff = (unsigned char*)malloc(bufflen);
                memcpy(buff, epoll->buff + res, bufflen);
                free(epoll->buff);
                epoll->buff = buff;
                epoll->bufflen = bufflen;
            }
        }
    } else {
        epoll->read(epoll, buffer);
    }
}

void Epoll_Write (EPOLL *epoll, const unsigned char *data, unsigned long len) {
    if (epoll->writeenable) {
        ssize_t res = write(epoll->fd, data, len);
        if (res == len) {
            return;
        }
        unsigned long bufflen;
        if (res <= 0) {
            bufflen = len;
            res = 0;
        } else if (res < len) {
            bufflen = len - res;
        }
        unsigned char *buff = (unsigned char*)malloc(epoll->bufflen);
        memcpy(buff, data + res, epoll->bufflen);
        mod_fd_at_poll(epoll, 1);
        epoll->buff = buff;
        epoll->bufflen = bufflen;
        epoll->writeenable = 0;
    } else {
        unsigned long bufflen = epoll->bufflen + len;
        unsigned char *buff = (unsigned char*)malloc(bufflen);
        if (epoll->bufflen) {
            memcpy(buff, epoll->buff, epoll->bufflen);
            free(epoll->buff);
        }
        memcpy(buff + epoll->bufflen, data, len);
        epoll->buff = buff;
        epoll->bufflen = bufflen;
    }
}

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
        Epoll_Event(evs[i].events, epoll);
    }
    goto LOOP;
}
