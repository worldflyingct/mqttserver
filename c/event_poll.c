#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include "event_poll.h"
#include "mqtt.h"
#include "smalloc.h"

#define MAXEVENTS      2048

static unsigned char buffer[512*1024];

static int epollfd = 0;
static int wait_count;
static struct epoll_event evs[MAXEVENTS];
EPOLL *remainepollhead = NULL;

EPOLL *add_fd_to_poll (int fd, int opt) {
    if (opt) {
        // 设置为非阻塞
        int fdflags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fdflags | O_NONBLOCK);

        int keepAlive = 1;    // 非0值，开启keepalive属性
        int keepIdle = 6;    // 如该连接在6秒内没有任何数据往来,则进行此TCP层的探测
        int keepInterval = 1; // 探测发包间隔为1秒
        int keepCount = 3;        // 尝试探测的最多3次数
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepAlive, sizeof(keepAlive));
        setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, (void*)&keepIdle, sizeof(keepIdle));
        setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, (void *)&keepInterval, sizeof(keepInterval));
        setsockopt(fd, SOL_TCP, TCP_KEEPCNT, (void *)&keepCount, sizeof(keepCount));
    }

    EPOLL *epoll = (EPOLL*)smalloc(sizeof(EPOLL));
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
        sfree(epoll);
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
    if (epoll->fd == 0) {
        return;
    }
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
        sfree(epoll->buff);
    }
    if (epoll->mqttpackagelen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        sfree(epoll->mqttpackage);
    }
    if (epoll->clientidlen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        sfree(epoll->clientid);
    }
    if (epoll->mqttwilltopiclen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        sfree(epoll->mqttwilltopic);
    }
    if (epoll->mqttwillmsglen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        sfree(epoll->mqttwillmsg);
    }
    if (epoll->httphead) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        sfree(epoll->httphead);
    }
    if (epoll->wspackagelen) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        free(epoll->wspackage);
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
                if (epoll->writeenable) {
                    epoll->writeenable = 0;
                    mod_fd_at_poll(epoll, 1);
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
                sfree(epoll->buff);
                epoll->bufflen = 0;
                epoll->writeenable = 1;
                mod_fd_at_poll(epoll, 0);
                return;
            }
            if (res < 0) {
                if (epoll->tls) {
                    int errcode = SSL_get_error(epoll->tls, res);
                    if (errcode == SSL_ERROR_WANT_READ) {
                        epoll->tlsok = 0;
                        mod_fd_at_poll(epoll, 0);
                    }
                }
                res = 0;
            }
            unsigned int bufflen = epoll->bufflen - res;
            unsigned char *buff = (unsigned char*)smalloc(bufflen);
            if (buff == NULL) {
                printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                return;
            }
            memcpy(buff, epoll->buff + res, bufflen);
            sfree(epoll->buff);
            epoll->buff = buff;
            epoll->bufflen = bufflen;
        } else {
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
        if (res < 0) {
            if (epoll->tls) {
                int errcode = SSL_get_error(epoll->tls, res);
                if (errcode == SSL_ERROR_WANT_READ) {
                    epoll->tlsok = 0;
                }
            }
            res = 0;
        }
        unsigned int bufflen = len - res;
        unsigned char *buff = (unsigned char*)smalloc(epoll->bufflen);
        if (buff == NULL) {
            printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
            return;
        }
        memcpy(buff, data + res, epoll->bufflen);
        epoll->buff = buff;
        epoll->bufflen = bufflen;
        epoll->writeenable = 0;
        if (epoll->tlsok) {
            mod_fd_at_poll(epoll, 1);
        }
    } else {
        unsigned int bufflen = epoll->bufflen + len;
        unsigned char *buff = (unsigned char*)smalloc(bufflen);
        if (buff == NULL) {
            printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
            return;
        }
        if (epoll->bufflen) {
            memcpy(buff, epoll->buff, epoll->bufflen);
            sfree(epoll->buff);
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
    for (int i = 0 ; i < wait_count ; i++) {
        EPOLL *epoll = evs[i].data.ptr;
        Epoll_Event(evs[i].events, epoll);
    }
    while (remainepollhead != NULL) {
        EPOLL *epoll = remainepollhead;
        remainepollhead = remainepollhead->tail;
        sfree(epoll);
    }
    goto LOOP;
}
