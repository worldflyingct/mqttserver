#ifndef __EVENT_POLL_H___
#define __EVENT_POLL_H___

#include <sys/epoll.h>
#include <openssl/ssl.h>
#include "ws.h"

typedef struct EPOLL EPOLL;
typedef void READ_FUNCTION (EPOLL *epoll, uint8_t *buff);
typedef void WRITE_FUNCTION (EPOLL *epoll, const uint8_t *data, uint64_t len);
typedef void DELETE_FUNCTION (EPOLL *epoll);

struct HTTPHEAD {
    char *httpmethod;
    char *httppath;
    char *httpversion;
    struct HTTPPARAM httpparam[30];
    uint16_t httpparam_size;
    int k;
    int p;
    int headlen;
};

struct EPOLL {
    int fd;
    READ_FUNCTION *read;
    WRITE_FUNCTION *write;
    DELETE_FUNCTION *delete;
    uint8_t *buff;
    uint32_t bufflen;
    uint8_t writeenable;
    EPOLL *head;
    EPOLL *tail;
    SSL *tls;
    uint8_t listenwrite;
    uint8_t tlsok; // 0为尚未握手成功，1为握手成功
    uint8_t mqttstate; // 0为未注，1为注册
    uint8_t *mqttpackage;
    uint32_t mqttpackagecap; // 当前包缓存的实际容量
    uint32_t mqttpackagelen; // 当前包的理论大小
    uint32_t mqttuselen; // 已经消耗的缓存
    uint8_t *clientid;
    uint16_t clientidlen;
    uint8_t *mqttwilltopic;
    uint16_t mqttwilltopiclen;
    uint8_t *mqttwillmsg;
    uint16_t mqttwillmsglen;
    struct SubScribeList *sbbl;
    uint16_t defaultkeepalive;
    uint16_t keepalive;
    struct HTTPHEAD *httphead;
    uint8_t wsstate; // 0为未注，1为注册
    uint8_t *wspackage;
    uint64_t wspackagecap; // 当前包缓存的实际容量
    uint64_t wspackagelen; // 当前包的理论大小
    uint64_t wsuselen; // 已经消耗的缓存
    uint8_t mqttversion;
};

int event_poll_create ();
void event_poll_loop ();
EPOLL *add_fd_to_poll (int fd, int out);
int mod_fd_at_poll (EPOLL *epoll, int eout);
void Epoll_Write (EPOLL *epoll, const uint8_t *data, uint64_t len);
void Epoll_Delete (EPOLL *epoll);

#endif
