#ifndef __EVENT_POLL_H___
#define __EVENT_POLL_H___

#define CONNECT         0x10
#define CONNACK         0x20
#define PUBLISH         0x30
#define PUBACK          0x40
#define PUBREC          0x50
#define PUBREL          0x60
#define PUBCOMP         0x70
#define SUBSCRIBE       0x80
#define SUBACK          0x90
#define UNSUBSCRIBE     0xa0
#define UNSUBACK        0xb0
#define PINGREQ         0xc0
#define PINGRESP        0xd0
#define DISCONNECT      0xe0

#include <sys/epoll.h>

typedef struct EPOLL EPOLL;
typedef void READ_FUNCTION (EPOLL *epoll, unsigned char *buff);
typedef void WRITE_FUNCTION (EPOLL *epoll, const unsigned char *data, unsigned long len);
typedef void DELETE_FUNCTION (EPOLL *epoll);

struct EPOLL {
    int fd;
    READ_FUNCTION *read;
    WRITE_FUNCTION *write;
    DELETE_FUNCTION *delete;
    unsigned char *buff;
    unsigned long bufflen;
    unsigned long writeenable;
    EPOLL *head;
    EPOLL *tail;
    unsigned char mqttstate; // 0为未注，1为注册
    unsigned char *mqttpackage;
    unsigned long mqttpackagelen; // 当前包的理论大小
    unsigned long mqttuselen; // 已经消耗的缓存
    struct SubScribeList *subscribelist;
    unsigned short keepalive;
    unsigned char wsstate; // 0为未注，1为注册
    unsigned char *wspackage;
    unsigned long wspackagelen; // 当前包的理论大小
    unsigned long wsuselen; // 已经消耗的缓存
    unsigned char nodestate; // 0为未注，1为注册
};

int event_poll_create ();
void event_poll_loop ();
EPOLL *add_fd_to_poll (int fd, int eout);
int mod_fd_at_poll (EPOLL *epoll, int eout);
void Epoll_Write (EPOLL *epoll, const unsigned char *data, unsigned long len);
void Epoll_Delete (EPOLL *epoll);

#endif
