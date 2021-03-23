#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include "event_poll.h"
#include "config.h"

static int listenfd;
struct TCP {
    int fd;
    EVENT_FUNCTION *func;
    WRITE_FUNCTION *write;
    DELETE_FUNCTION *delete;
    unsigned char mqttstate; // 0为未注，1为注册
    unsigned char *mqttpackage;
    unsigned int mqttpackagelen; // 当前包的理论大小
    unsigned int mqttuselen; // 已经消耗的缓存
    struct int *head;
    struct int *tail;
};
static struct TCP *remaintcp = NULL;
extern unsigned char *buff;

static int Tcp_Delete_Connect (struct TCP *tcp) {
    remove_fd_from_poll(tcp->epoll);
    tcp->tail = remaintcp;
    remaintcp = tcp;
}

static int Tcp_Event_Handler (int event, void *ptr) { // 作为mqtt处理
    struct TCP *tcp = ptr;
    if (event & (EPOLLERR|EPOLLHUP|EPOLLRDHUP)) { // 错误异常处理
        Tcp_Delete_Connect(tcp);
        return 0;
    }
}

static int Tcp_New_Connect (int event, void *ptr) {
    struct sockaddr_in sin;
    socklen_t in_addr_len = sizeof(struct sockaddr_in);
    int fd = accept(listenfd, (struct sockaddr*)&sin, &in_addr_len);
    if (fd < 0) {
        printf("accept a new fd fail, in %s, at %d\n", __FILE__, __LINE__);
        return -1;
    }
    struct TCP *tcp;
    if (remaintcp) {
        tcp = remaintcp;
        remaintcp = remaintcp->tail;
    } else {
        tcp = (struct TCP*)malloc(sizeof(struct WS));
        if (tcp == NULL) {
            printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
            return -2;
        }
    }
    tcp->fd = fd;
    struct EPOLL *epoll = add_fd_to_poll(fd, Tcp_Event_Handler, ws);
    if (epoll == NULL) {
        printf("add fd to poll fail, in %s, at %d\n", __FILE__, __LINE__);
        close(fd);
        tcp->tail = remaintcp;
        remaintcp = tcp;
        return -3;
    }
    tcp->epoll = epoll;
    ws->package = NULL;
    ws->packagelen = 0;
    ws->uselen = 0;
    return 0;
}

int Tcp_Create () {
    struct ConfigData *configdata = GetConfig ();
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        printf("create socket fail, in %s, at %d\n", __FILE__, __LINE__);
        return -1;
    }
    int on = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(struct sockaddr_in));
    sin.sin_family = AF_INET; // ipv4
    sin.sin_addr.s_addr = INADDR_ANY; // 任意ip
    sin.sin_port = htons(configdata->tcpport);
    if (bind(listenfd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        printf("port %d bind fail, in %s, at %d\n", configdata->tcpport, __FILE__, __LINE__);
        close(listenfd);
        return -2;
    }
    if (listen(listenfd, 16) < 0) {
        printf("listen port %d fail, in %s, at %d\n", configdata->tcpport, __FILE__, __LINE__);
        close(listenfd);
        return -3;
    }
    struct EPOLL *epoll = add_fd_to_poll(listenfd, Tcp_New_Connect, NULL);
    if (epoll == NULL) {
        close(listenfd);
        return -4;
    }
    return 0;
}
