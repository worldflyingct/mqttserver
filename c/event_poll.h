#ifndef __EVENT_POLL_H___
#define __EVENT_POLL_H___

#include <sys/epoll.h>
#include <openssl/ssl.h>
#include "ws.h"

typedef struct EPOLL EPOLL;
typedef void READ_FUNCTION (EPOLL *epoll, unsigned char *buff);
typedef void WRITE_FUNCTION (EPOLL *epoll, const unsigned char *data, unsigned long len);
typedef void DELETE_FUNCTION (EPOLL *epoll);

struct HTTPHEAD {
    char *httpmethod;
    char *httppath;
    char *httpversion;
    struct HTTPPARAM httpparam[30];
    unsigned short httpparam_size;
    int k;
    int p;
    int headlen;
};

struct EPOLL {
    int fd;
    READ_FUNCTION *read;
    WRITE_FUNCTION *write;
    DELETE_FUNCTION *delete;
    unsigned char *buff;
    unsigned int bufflen;
    unsigned char writeenable;
    EPOLL *head;
    EPOLL *tail;
    SSL *tls;
    unsigned char listenwrite;
    unsigned char tlsok; // 0为尚未握手成功，1为握手成功
    unsigned char mqttstate; // 0为未注，1为注册
    unsigned char *mqttpackage;
    unsigned int mqttpackagecap; // 当前包缓存的实际容量
    unsigned int mqttpackagelen; // 当前包的理论大小
    unsigned int mqttuselen; // 已经消耗的缓存
    unsigned char *clientid;
    unsigned short clientidlen;
    unsigned char *mqttwilltopic;
    unsigned short mqttwilltopiclen;
    unsigned char *mqttwillmsg;
    unsigned short mqttwillmsglen;
    struct SubScribeList *sbbl;
    unsigned short keepalive;
    struct HTTPHEAD *httphead;
    unsigned char wsstate; // 0为未注，1为注册
    unsigned char *wspackage;
    unsigned long wspackagecap; // 当前包缓存的实际容量
    unsigned long wspackagelen; // 当前包的理论大小
    unsigned long wsuselen; // 已经消耗的缓存
};

int event_poll_create ();
void event_poll_loop ();
EPOLL *add_fd_to_poll (int fd, int opt);
int mod_fd_at_poll (EPOLL *epoll, int eout);
void Epoll_Write (EPOLL *epoll, const unsigned char *data, unsigned long len);
void Epoll_Delete (EPOLL *epoll);

#endif
