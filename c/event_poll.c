#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include "event_poll.h"
#include "mqtt.h"

#define MAXEVENTS      2048

static unsigned char buffer[512*1024];

static int epollfd = 0;
static int wait_count;
static struct epoll_event evs[MAXEVENTS];
EPOLL *remainepollhead = NULL;

EPOLL *add_fd_to_poll (int fd) {
    // 设置为非阻塞
    int fdflags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fdflags | O_NONBLOCK);

    EPOLL *epoll = (EPOLL*)malloc(sizeof(EPOLL));
    if (epoll == NULL) {
        printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
        return NULL;
    }
    memset(epoll, 0, sizeof(EPOLL));
    epoll->fd = fd;
    struct epoll_event ev;
    ev.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLIN | EPOLLOUT;
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
    if (epoll->mqttstate) {
        DeleteMqttClient(epoll, buffer);
    }
    struct epoll_event ev;
    // printf("fd:%d, in %s, at %d\n", epoll->fd, __FILE__, __LINE__);
    epoll_ctl(epollfd, EPOLL_CTL_DEL, epoll->fd, &ev);
    close(epoll->fd);
    epoll->fd = 0;
    if (epoll->bufflen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        free(epoll->buff);
    }
    if (epoll->mqttpackagelen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        free(epoll->mqttpackage);
    }
    if (epoll->clientidlen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        free(epoll->clientid);
    }
    if (epoll->mqttwilltopiclen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        free(epoll->mqttwilltopic);
    }
    if (epoll->mqttwillmsglen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        free(epoll->mqttwillmsg);
    }
    if (epoll->httphead) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        free(epoll->httphead);
    }
    if (epoll->wspackagelen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        free(epoll->wspackage);
    }
    if (epoll->tls) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        SSL_shutdown(epoll->tls);
        SSL_free(epoll->tls);
    }
    epoll->tail = remainepollhead;
    remainepollhead = epoll;
}

static void Epoll_Event (int event, EPOLL *epoll) {
    if (epoll->fd) {
        if (event & (EPOLLERR|EPOLLHUP|EPOLLRDHUP)) { // 错误异常处理
            // printf("in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
        } else if (epoll->tls && !epoll->tlsok) {
            int r_code = SSL_accept(epoll->tls);
            if(r_code == 1) {
                epoll->tlsok = 1;
                if (epoll->writeenable) {
                    mod_fd_at_poll(epoll, 1);
                    epoll->writeenable = 0;
                }
            } else {
                int errcode = SSL_get_error(epoll->tls, r_code);
                if (errcode == SSL_ERROR_WANT_READ) {
                    mod_fd_at_poll(epoll, 0);
                    epoll->writeenable = 1;
                } else if (errcode == SSL_ERROR_WANT_WRITE) {
                    mod_fd_at_poll(epoll, 1);
                    epoll->writeenable = 0;
                } else {
                    Epoll_Delete(epoll);
                }
            }
        } else if (event & EPOLLOUT) {
            // printf("in %s, at %d\n", __FILE__, __LINE__);
            epoll->writeenable = 1;
            if (epoll->bufflen > 0) {
                ssize_t res;
                if (epoll->tls) {
                    res = SSL_write(epoll->tls, epoll->buff, epoll->bufflen);
                } else {
                    res = write(epoll->fd, epoll->buff, epoll->bufflen);
                }
                if (res == epoll->bufflen) {
                    free(epoll->buff);
                    epoll->buff = NULL;
                    epoll->bufflen = 0;
                    mod_fd_at_poll(epoll, 0);
                } else if ( 0 <= res && res < epoll->bufflen) {
                    unsigned long bufflen = epoll->bufflen - res;
                    unsigned char *buff = (unsigned char*)malloc(bufflen);
                    memcpy(buff, epoll->buff + res, bufflen);
                    free(epoll->buff);
                    epoll->buff = buff;
                    epoll->bufflen = bufflen;
                    epoll->writeenable = 0;
                }
            } else {
                mod_fd_at_poll(epoll, 0);
            }
        } else {
            // printf("in %s, at %d\n", __FILE__, __LINE__);
            epoll->read(epoll, buffer);
        }
    }
}

void Epoll_Write (EPOLL *epoll, const unsigned char *data, unsigned long len) {
    if (epoll->writeenable) {
        ssize_t res;
        if (epoll->tls) {
            res = SSL_write(epoll->tls, data, len);
        } else {
            res = write(epoll->fd, data, len);
        }
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
    // printf("wait_count:%d, in %s, at %d\n", wait_count, __FILE__, __LINE__);
    for (int i = 0 ; i < wait_count ; i++) {
        EPOLL *epoll = evs[i].data.ptr;
        Epoll_Event(evs[i].events, epoll);
    }
    while (remainepollhead != NULL) {
        EPOLL *epoll = remainepollhead;
        remainepollhead = remainepollhead->tail;
        free(epoll);
    }
    goto LOOP;
}
