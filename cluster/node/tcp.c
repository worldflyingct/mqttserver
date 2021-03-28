#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include "event_poll.h"
#include "config.h"
#include "mqtt.h"

static int Tcp_Delete_Connect (EPOLL *epoll) {
    remove_fd_from_poll(epoll);
}

static int Tcp_Write_Connect (EPOLL *epoll, const unsigned char *data, unsigned long len) {
    write(epoll->fd, data, len);
}

static int Tcp_Event_Handler (int event, EPOLL *epoll, unsigned char *buff) { // 作为mqtt处理
    if (event & (EPOLLERR|EPOLLHUP|EPOLLRDHUP)) { // 错误异常处理
        Tcp_Delete_Connect(epoll);
        return 0;
    }
    ssize_t len = read(epoll->fd, buff, 512*1024);
    if (len < 0) {
        return -1;
    }
    HandleMqttClientRequest(epoll, buff, len);
    return 0;
}

static int Tcp_New_Connect (int event, EPOLL *e, unsigned char *buff) {
    struct sockaddr_in sin;
    socklen_t in_addr_len = sizeof(struct sockaddr_in);
    int fd = accept(e->fd, (struct sockaddr*)&sin, &in_addr_len);
    if (fd < 0) {
        printf("accept a new fd fail, in %s, at %d\n", __FILE__, __LINE__);
        return -1;
    }
    EPOLL *epoll = add_fd_to_poll(fd, 0);
    if (epoll == NULL) {
        printf("add fd to poll fail, in %s, at %d\n", __FILE__, __LINE__);
        close(fd);
        return -2;
    }
    epoll->func = Tcp_Event_Handler;
    epoll->write = Tcp_Write_Connect;
    epoll->delete = Tcp_Delete_Connect;
    epoll->mqttstate = 0;
    epoll->mqttpackage = NULL;
    epoll->mqttpackagelen = 0;
    epoll->mqttuselen = 0;
    epoll->buff = NULL;
    epoll->bufflen = 0;
    epoll->uselen = 0;
    return 0;
}

int Tcp_Create () {
    struct ConfigData *configdata = GetConfig ();
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
    if (listen(fd, 16) < 0) {
        printf("listen port %d fail, in %s, at %d\n", configdata->tcpport, __FILE__, __LINE__);
        close(fd);
        return -3;
    }
    EPOLL *epoll = add_fd_to_poll(fd, 0);
    if (epoll == NULL) {
        close(fd);
        return -4;
    }
    epoll->func = Tcp_New_Connect;
    return 0;
}
