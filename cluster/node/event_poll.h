#ifndef __EVENT_POLL_H___
#define __EVENT_POLL_H___

#include <sys/epoll.h>

typedef struct EPOLL EPOLL;
typedef int EVENT_FUNCTION (int event, EPOLL *epoll, unsigned char *buff);
typedef int WRITE_FUNCTION (EPOLL *epoll, const unsigned char *data, unsigned long len);
typedef int DELETE_FUNCTION (EPOLL *epoll);

struct EPOLL {
    int fd;
    EVENT_FUNCTION *func;
    WRITE_FUNCTION *write;
    DELETE_FUNCTION *delete;
    unsigned char mqttstate; // 0为未注，1为注册
    unsigned char *mqttpackage;
    unsigned long mqttpackagelen; // 当前包的理论大小
    unsigned long mqttuselen; // 已经消耗的缓存
    unsigned short keepalive;
    unsigned char wsstate; // 0为未注，1为注册
    unsigned char *wspackage;
    unsigned long wspackagelen; // 当前包的理论大小
    unsigned long wsuselen; // 已经消耗的缓存
    unsigned char nodestate; // 0为未注，1为注册
    unsigned char *buff;
    unsigned long bufflen;
    unsigned long writeenable;
    EPOLL *head;
    EPOLL *tail;
};

int event_poll_create ();
void event_poll_loop ();
EPOLL *add_fd_to_poll (int fd, int eout);
int mod_fd_at_poll (EPOLL *epoll, int eout);
void remove_fd_from_poll (EPOLL *epoll);

#endif
