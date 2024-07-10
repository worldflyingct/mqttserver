#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include "event_poll.h"
#include "config.h"
#include "mqtt.h"

static void Tcp_Read_Handler (EPOLL *epoll, uint8_t *buff) { // 作为mqtt处理
    ssize_t len;
    if (epoll->tls) {
        len = SSL_read(epoll->tls, buff, 512*1024);
    } else {
        len = read(epoll->fd, buff, 512*1024);
    }
    if (len < 0) {
        if (epoll->tls) {
            int errcode = SSL_get_error(epoll->tls, len);
            if (errcode == SSL_ERROR_WANT_WRITE) {
                epoll->tlsok = 0;
                mod_fd_at_poll(epoll, 1);
                epoll->writeenable = 0;
            }
        }
        return;
    }
    HandleMqttClientRequest(epoll, buff, len);
}

static void Tcp_New_Connect (EPOLL *e, uint8_t *buff) {
    struct sockaddr_in sin;
    socklen_t in_addr_len = sizeof(struct sockaddr_in);
    int fd = accept(e->fd, (struct sockaddr*)&sin, &in_addr_len);
    if (fd < 0) {
        printf("accept a new fd fail, in %s, at %d\n", __FILE__, __LINE__);
        return;
    }
    EPOLL *epoll = add_fd_to_poll(fd, 1);
    if (epoll == NULL) {
        printf("add fd to poll fail, in %s, at %d\n", __FILE__, __LINE__);
        close(fd);
        return;
    }
    epoll->read = Tcp_Read_Handler;
    epoll->write = Epoll_Write;
    if (e->tls) {
        struct ConfigData *configdata = GetConfig();
        SSL *tls = SSL_new(configdata->ctx);
        SSL_set_fd(tls, fd);
        epoll->tls = tls;
        SSL_set_accept_state(tls);
    }
}

int Tcp_Create () {
    struct ConfigData *configdata = GetConfig ();
    if (configdata->tcpport) {
        int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP );
        if (fd < 0) {
            printf("create socket fail, in %s, at %d\n", __FILE__, __LINE__);
            return -1;
        }
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(struct sockaddr_in));
        sin.sin_family = AF_INET; // ipv4
        sin.sin_addr.s_addr = INADDR_ANY; // 任意ip
        sin.sin_port = htons(configdata->tcpport);
        if (bind(fd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
            printf("port %d bind fail, in %s, at %d\n", configdata->tcpport, __FILE__, __LINE__);
            close(fd);
            return -2;
        }
        if (listen(fd, 32) < 0) {
            printf("listen port %d fail, in %s, at %d\n", configdata->tcpport, __FILE__, __LINE__);
            close(fd);
            return -3;
        }
        EPOLL *epoll = add_fd_to_poll(fd, 1);
        if (epoll == NULL) {
            printf("add fd to poll fail, fd: %d, in %s, at %d\n", fd, __FILE__, __LINE__);
            close(fd);
            return -4;
        }
        epoll->read = Tcp_New_Connect;
    }
    if (configdata->tlsport) {
        int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP );
        if (fd < 0) {
            printf("create socket fail, in %s, at %d\n", __FILE__, __LINE__);
            return -5;
        }
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(struct sockaddr_in));
        sin.sin_family = AF_INET; // ipv4
        sin.sin_addr.s_addr = INADDR_ANY; // 任意ip
        sin.sin_port = htons(configdata->tlsport);
        if (bind(fd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
            printf("port %d bind fail, in %s, at %d\n", configdata->tlsport, __FILE__, __LINE__);
            close(fd);
            return -6;
        }
        if (listen(fd, 32) < 0) {
            printf("listen port %d fail, in %s, at %d\n", configdata->tlsport, __FILE__, __LINE__);
            close(fd);
            return -7;
        }
        EPOLL *epoll = add_fd_to_poll(fd, 0);
        if (epoll == NULL) {
            printf("add fd to poll fail, fd: %d, in %s, at %d\n", fd, __FILE__, __LINE__);
            close(fd);
            return -8;
        }
        epoll->read = Tcp_New_Connect;
        epoll->tls = (SSL*)1;
        epoll->tlsok = 1;
    }
    return 0;
}
