#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <time.h>
#include "event_poll.h"
#include "config.h"

static int Connect_Event_Function (int event, EPOLL *epoll, unsigned char *buff) {
    if (event & (EPOLLERR|EPOLLHUP|EPOLLRDHUP)) { // 错误异常处理
        Connect_Delete_Function(epoll);
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
        ssize_t res = read(epoll->fd, buff, 512*1024);
        if (res <= 0) {

        }
    }
    return 0;
}

static int Connect_Write_Function (EPOLL *epoll, const unsigned char *data, unsigned long len) {
    if (epoll->writeenable) {
        ssize_t res = write(epoll->fd, data, len);
        if (res == len) {
            return 0;
        }
        if (res <= 0) {
            epoll->bufflen = len;
            res = 0;
        } else if (res < len) {
            epoll->bufflen = len - res;
        }
        epoll->buff = (unsigned char*)malloc(epoll->bufflen);
        memcpy(epoll->buff, data + res, epoll->bufflen);
        mod_fd_at_poll(epoll, 1);
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

static int Connect_Delete_Function (EPOLL *epoll) {
    if (epoll->bufflen) {
        free(epoll->buff);
        epoll->buff = NULL;
    }
    remove_fd_from_poll(epoll);
}

int Connect_Create () {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    EPOLL *epoll = add_fd_to_poll(fd, 1);
    if (epoll == NULL) {
        close(fd);
        return -1;
    }
    struct ConfigData *configdata = GetConfig();
    struct hostent *ip = gethostbyname(configdata->serverdomain);
    if (ip == NULL) {
        remove_fd_from_poll(epoll);
        return -2;
    }
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(configdata->serverport);
    unsigned char *addr = ip->h_addr_list[0];
    if (ip->h_addrtype == AF_INET) { // ipv4
        memcpy(&sin.sin_addr, addr, 4);
    } else if (ip->h_addrtype == AF_INET6) { // ipv6
        printf("not support ipv6, in %s, at %d\n", __FILE__, __LINE__);
        return -3;
    }
    if(connect(fd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        printf("connect server fail, fd:%d, in %s, at %d\n", fd, __FILE__, __LINE__);
        remove_fd_from_poll(epoll);
        return -4;
    }
    epoll->func = Connect_Event_Function;
    epoll->write = Connect_Write_Function;
    epoll->delete = Connect_Delete_Function;
    epoll->buff = NULL;
    epoll->bufflen = 0;
    epoll->uselen = 0;
    epoll->nodestate = 0;
    epoll->writeenable = 0;
    struct ConfigData *configdata = GetConfig();
    unsigned char data[128];
    unsigned char out[32];
    time_t timestamp = time(NULL);
    int len = sprintf(data, "%s&%u&%s", configdata->mqttuser, timestamp, configdata->mqttkey);
    sha256(data, len, out);
    len = sprintf(data, "%s&%u&" \
                            "%02x%02x%02x%02x%02x%02x%02x%02x" \
                            "%02x%02x%02x%02x%02x%02x%02x%02x" \
                            "%02x%02x%02x%02x%02x%02x%02x%02x" \
                            "%02x%02x%02x%02x%02x%02x%02x%02x",
                            configdata->mqttuser, timestamp, out[0], out[1],
                            out[2], out[3], out[4], out[5], out[6], out[7], out[8],
                            out[9], out[10], out[11], out[12], out[13], out[14], out[15],
                            out[16], out[17], out[18], out[19], out[20], out[21], out[22],
                            out[23], out[24], out[25], out[26], out[27], out[28], out[29],
                            out[30], out[31]);
    epoll->write(epoll, data, len);
}
