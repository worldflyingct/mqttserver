#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include "event_poll.h"
#include "mqtt.h"
#include "smalloc.h"
#include "config.h"

#define MAXEVENTS      2048

static uint8_t buffer[512*1024];

static int epollfd = 0;
static int wait_count;
static struct epoll_event evs[MAXEVENTS];
EPOLL *remainepollhead = NULL;

EPOLL *add_fd_to_poll (int fd, int out) {
    // 设置为非阻塞
    int fdflags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fdflags | O_NONBLOCK);

    EPOLL *epoll = (EPOLL*)smalloc(sizeof(EPOLL), __FILE__, __LINE__);
    if (epoll == NULL) {
        return NULL;
    }
    memset(epoll, 0, sizeof(EPOLL));
    epoll->fd = fd;
    epoll->listenwrite = 1;
    struct epoll_event ev;
    if (out) {
        ev.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLIN | EPOLLOUT;
    } else {
        ev.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLIN;
    }
    ev.data.ptr = epoll;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev)) {
        printf("add fd:%d to poll, in %s, at %d\n", fd, __FILE__, __LINE__);
        sfree(epoll, __FILE__, __LINE__);
        return NULL;
    }
    return epoll;
}

int mod_fd_at_poll (EPOLL *epoll, int eout) {
    struct epoll_event ev;
    if (eout) {
        if (epoll->listenwrite) {
            return 0;
        }
        ev.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLIN | EPOLLOUT;
        epoll->listenwrite = 1;
    } else {
        if (!epoll->listenwrite) {
            return 0;
        }
        ev.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLIN;
        epoll->listenwrite = 0;
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
    if (epoll->fd == 0) {
        return;
    }
    struct epoll_event ev;
    // printf("fd:%d, in %s, at %d\n", epoll->fd, __FILE__, __LINE__);
    epoll_ctl(epollfd, EPOLL_CTL_DEL, epoll->fd, &ev);
    close(epoll->fd);
    epoll->fd = 0;
    if (epoll->mqttstate) {
        DeleteMqttClient(epoll);
    }
    if (epoll->bufflen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        sfree(epoll->buff, __FILE__, __LINE__);
    }
    if (epoll->mqttpackagecap) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        sfree(epoll->mqttpackage, __FILE__, __LINE__);
    }
    if (epoll->clientidlen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        sfree(epoll->clientid, __FILE__, __LINE__);
    }
    if (epoll->mqttwilltopiclen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        sfree(epoll->mqttwilltopic, __FILE__, __LINE__);
    }
    if (epoll->mqttwillmsglen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        sfree(epoll->mqttwillmsg, __FILE__, __LINE__);
    }
    if (epoll->httphead) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        sfree(epoll->httphead, __FILE__, __LINE__);
    }
    if (epoll->wspackagecap) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        sfree(epoll->wspackage, __FILE__, __LINE__);
    }
    if (epoll->tls) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        // SSL_shutdown(epoll->tls);
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
            int r_code = SSL_do_handshake(epoll->tls);
            if(r_code == 1) {
                // printf("in %s, at %d\n", __FILE__, __LINE__);
                epoll->tlsok = 1;
                mod_fd_at_poll(epoll, 1);
            } else {
                int errcode = SSL_get_error(epoll->tls, r_code);
                if (errcode == SSL_ERROR_WANT_READ) {
                    mod_fd_at_poll(epoll, 0);
                } else if (errcode == SSL_ERROR_WANT_WRITE) {
                    mod_fd_at_poll(epoll, 1);
                } else {
                    printf("tls handshake fail, err: %d, in %s, at %d\n", errcode, __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                }
            }
        } else if (event & EPOLLOUT) {
            if (epoll->bufflen == 0) {
                epoll->writeenable = 1;
                mod_fd_at_poll(epoll, 0);
                return;
            }
            ssize_t res;
            if (epoll->tls) {
                res = SSL_write(epoll->tls, epoll->buff, epoll->bufflen);
            } else {
                res = write(epoll->fd, epoll->buff, epoll->bufflen);
            }
            if (res == epoll->bufflen) {
                sfree(epoll->buff, __FILE__, __LINE__);
                epoll->bufflen = 0;
                epoll->writeenable = 1;
                mod_fd_at_poll(epoll, 0);
            } else if (res > 0) {
                uint32_t bufflen = epoll->bufflen - res;
                uint8_t *buff = (uint8_t*)smalloc(bufflen, __FILE__, __LINE__);
                if (buff == NULL) {
                    Epoll_Delete(epoll);
                    return;
                }
                memcpy(buff, epoll->buff + res, bufflen);
                sfree(epoll->buff, __FILE__, __LINE__);
                epoll->buff = buff;
                epoll->bufflen = bufflen;
            } else if (res < 0) {
                if (epoll->tls) {
                    int errcode = SSL_get_error(epoll->tls, res);
                    if (errcode == SSL_ERROR_WANT_READ) {
                        epoll->tlsok = 0;
                        mod_fd_at_poll(epoll, 0);
                    }
                }
            }
        } else {
            epoll->read(epoll, buffer);
        }
    }
}

void Epoll_Write (EPOLL *epoll, const uint8_t *data, uint64_t len) {
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
        if (res < 0) {
            if (epoll->tls) {
                int errcode = SSL_get_error(epoll->tls, res);
                if (errcode == SSL_ERROR_WANT_READ) {
                    epoll->tlsok = 0;
                }
            }
            res = 0;
        }
        uint32_t bufflen = len - res;
        uint8_t *buff = (uint8_t*)smalloc(bufflen, __FILE__, __LINE__);
        if (buff == NULL) {
            Epoll_Delete(epoll);
            return;
        }
        memcpy(buff, data + res, bufflen);
        epoll->buff = buff;
        epoll->bufflen = bufflen;
        epoll->writeenable = 0;
        if (epoll->tlsok) {
            mod_fd_at_poll(epoll, 1);
        }
    } else {
        uint32_t bufflen = epoll->bufflen + len;
        uint8_t *buff = (uint8_t*)smalloc(bufflen, __FILE__, __LINE__);
        if (buff == NULL) {
            Epoll_Delete(epoll);
            return;
        }
        if (epoll->bufflen) {
            memcpy(buff, epoll->buff, epoll->bufflen);
            sfree(epoll->buff, __FILE__, __LINE__);
        }
        memcpy(buff + epoll->bufflen, data, len);
        epoll->buff = buff;
        epoll->bufflen = bufflen;
    }
}

int event_poll_create () {
    int fdflags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, fdflags | O_NONBLOCK);
    // 直接输出printf的内容，这是影响性能的。
    setvbuf(stdout, NULL, _IONBF, 0);

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
    for (int i = 0 ; i < wait_count ; ++i) {
        EPOLL *epoll = evs[i].data.ptr;
        Epoll_Event(evs[i].events, epoll);
    }
    while (remainepollhead != NULL) {
        EPOLL *epoll = remainepollhead;
        remainepollhead = remainepollhead->tail;
        sfree(epoll, __FILE__, __LINE__);
    }
    goto LOOP;
}
