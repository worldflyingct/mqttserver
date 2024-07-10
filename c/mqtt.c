#include <stdio.h>
#include <string.h>
#include <time.h>
#include "event_poll.h"
#include "mqtt.h"
#include "config.h"
#include "sha256.h"
#include "smalloc.h"

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

static const uint8_t connsuccess[]     = {CONNACK, 0x02, 0x00, 0x00};
static const uint8_t connvererr[]      = {CONNACK, 0x02, 0x00, 0x01};
static const uint8_t connvererrv5[]    = {CONNACK, 0x03, 0x00, 0x84, 0x00};
static const uint8_t connsererr[]      = {CONNACK, 0x02, 0x00, 0x03};
static const uint8_t connsererrv5[]    = {CONNACK, 0x03, 0x00, 0x80, 0x00};
static const uint8_t connautherrv5[]   = {CONNACK, 0x03, 0x00, 0x8c, 0x00};
static const uint8_t connloginfail[]   = {CONNACK, 0x02, 0x00, 0x04};
static const uint8_t connloginfailv5[] = {CONNACK, 0x03, 0x00, 0x86, 0x00};
static const uint8_t connnologin[]     = {CONNACK, 0x02, 0x00, 0x05};
static const uint8_t connnologinv5[]   = {CONNACK, 0x03, 0x00, 0x8c, 0x00};
static const uint8_t pingresp[]        = {PINGRESP, 0x00};

struct TopicToEpoll {
    EPOLL *epoll;
    struct TopicToEpoll *head;
    struct TopicToEpoll *tail;
};

struct TopicList {
    uint8_t *topic;
    uint16_t topiclen;
    struct TopicToEpoll *tte;
    struct TopicList *head;
    struct TopicList *tail;
};
static struct TopicList *topiclisthead = NULL;
static EPOLL *epollhead = NULL;

uint32_t GetClientsNum () {
    uint32_t num = 0;
    EPOLL *epoll = epollhead;
    while (epoll != NULL) {
        ++num;
        epoll = epoll->tail;
    }
    return num;
}

uint8_t CheckClientStatus (char *clientid, uint32_t clientidlen) {
    EPOLL *epoll = epollhead;
    while (epoll != NULL) {
        if (clientidlen == epoll->clientidlen && !memcmp(clientid, epoll->clientid, clientidlen)) {
            return 1;
        }
        epoll = epoll->tail;
    }
    return 0;
}

void ShowClients () {
    EPOLL *epoll = epollhead;
    while (epoll != NULL) {
        uint8_t clientid[epoll->clientidlen+1];
        memcpy(clientid, epoll->clientid, epoll->clientidlen);
        clientid[epoll->clientidlen] = '\0';
        printf("clientid:%s topic:", clientid);
        struct SubScribeList *sbbl = epoll->sbbl;
        while (sbbl != NULL) {
            uint8_t topic[sbbl->topiclist->topiclen+1];
            memcpy(topic, sbbl->topiclist->topic, sbbl->topiclist->topiclen);
            topic[sbbl->topiclist->topiclen] = '\0';
            printf("%s ", topic);
            sbbl = sbbl->tail;
        }
        printf("\n");
        epoll = epoll->tail;
    }
}

void ShowTopics () {
    struct TopicList *topiclist = topiclisthead;
    while (topiclist != NULL) {
        uint8_t topic[topiclist->topiclen+1];
        memcpy(topic, topiclist->topic, topiclist->topiclen);
        topic[topiclist->topiclen] = '\0';
        printf("topic:%s clientid:", topic);
        struct TopicToEpoll *tte = topiclist->tte;
        while (tte != NULL) {
            uint8_t clientid[tte->epoll->clientidlen+1];
            memcpy(clientid, tte->epoll->clientid, tte->epoll->clientidlen);
            clientid[tte->epoll->clientidlen] = '\0';
            printf("%s ", clientid);
            tte = tte->tail;
        }
        printf("\n");
        topiclist = topiclist->tail;
    }
}

static void SendToClient (uint8_t *package, uint32_t packagelen, uint8_t *packagev5, uint32_t packagev5len, uint8_t *topic, uint16_t topiclen) {
    struct TopicList *topiclist = topiclisthead;
    while (topiclist != NULL) {
        if (topiclist->topiclen == topiclen && !memcmp(topiclist->topic, topic, topiclen)) {
            break;
        }
        topiclist = topiclist->tail;
    }
    if (topiclist != NULL) {
        struct TopicToEpoll *tte = topiclist->tte;
        while (tte != NULL) {
            EPOLL *epoll = tte->epoll;
            if (epoll->mqttversion == 5) {
                if (packagev5 != NULL && packagev5len > 0) {
                    epoll->write(epoll, packagev5, packagev5len);
                }
            } else {
                if (package != NULL && packagelen > 0) {
                    epoll->write(epoll, package, packagelen);
                }
            }
            epoll->keepalive = epoll->defaultkeepalive;
            tte = tte->tail;
        }
    }
}

static uint8_t* CreatePublishData (uint8_t *topic, uint16_t topiclen, uint8_t *msg, uint32_t msglen, uint8_t mqttversion, uint32_t *reslen) {
    uint32_t len = 2 + topiclen + msglen;
    if (mqttversion == 5) {
        len += 1; // mqttv5多定义了一个PUBLISH Properties，这里需要送一个Property Length为0的字段
    }
    uint32_t packagelen;
    uint32_t offset;
    if (len < 0x80) {
        packagelen = len + 2;
        offset = 2;
    } else if (len < 0x4000) {
        packagelen = len + 3;
        offset = 3;
    } else if (len < 0x200000) {
        packagelen = len + 4;
        offset = 4;
    } else {
        packagelen = len + 5;
        offset = 5;
    }
    uint8_t* package = (uint8_t*)smalloc(packagelen, __FILE__, __LINE__);
    if (package == NULL) {
        *reslen = 0;
        return package;
    }
    uint8_t n = 0;
    while (len > 0) {
        package[n] |= 0x80;
        ++n;
        package[n] = len & 0x7f;
        len >>= 7;
    }
    package[0] = PUBLISH;
    package[offset] = topiclen >> 8;
    package[offset+1] = topiclen;
    offset += 2;
    memcpy(package + offset, topic, topiclen);
    offset += topiclen;
	if (mqttversion == 5) {
		package[offset] = 0;
		offset += 1;
	}
    memcpy(package + offset, msg, msglen);
    *reslen = packagelen;
    return package;
}

static void PublishData (uint8_t *topic, uint16_t topiclen, uint8_t *msg, uint32_t msglen) {
    uint32_t packagelen, packagev5len;
    uint8_t* package = CreatePublishData(topic, topiclen, msg, msglen, 4, &packagelen);
    uint8_t* packagev5 = CreatePublishData(topic, topiclen, msg, msglen, 5, &packagev5len);
    SendToClient(package, packagelen, packagev5, packagev5len, topic, topiclen);
    if (package != NULL) {
        sfree(package, __FILE__, __LINE__);
    }
    if (packagev5 != NULL) {
        sfree(packagev5, __FILE__, __LINE__);
    }
}

static void UnSubScribeFunc (EPOLL *epoll, struct SubScribeList *sbbl) {
    if (sbbl->head) {
        sbbl->head->tail = sbbl->tail;
    } else {
        epoll->sbbl = sbbl->tail;
    }
    if (sbbl->tail) {
        sbbl->tail->head = sbbl->head;
    }
    struct TopicList *topiclist = sbbl->topiclist;
    sfree(sbbl, __FILE__, __LINE__);
    struct TopicToEpoll *tte = topiclist->tte;
    while (tte != NULL) {
        if (tte->epoll == epoll) {
            break;
        }
        tte = tte->tail;
    }
    if (tte != NULL) {
        if (tte->head) {
            tte->head->tail = tte->tail;
        } else {
            topiclist->tte = tte->tail;
        }
        if (tte->tail) {
            tte->tail->head = tte->head;
        }
        sfree(tte, __FILE__, __LINE__);
    }
    if (topiclist->tte == NULL) {
        if (topiclist->head) {
            topiclist->head->tail = topiclist->tail;
        } else {
            topiclisthead = topiclist->tail;
        }
        if (topiclist->tail) {
            topiclist->tail->head = topiclist->head;
        }
        sfree(topiclist->topic, __FILE__, __LINE__);
        sfree(topiclist, __FILE__, __LINE__);
    }
}

void DeleteMqttClient (EPOLL *epoll) {
    struct SubScribeList *sbbl = epoll->sbbl;
    while (sbbl != NULL) {
        struct SubScribeList *next = sbbl->tail;
        UnSubScribeFunc(epoll, sbbl);
        sbbl = next;
    }
    if (epoll->head) {
        epoll->head->tail = epoll->tail;
    } else {
        epollhead = epoll->tail;
    }
    if (epoll->tail) {
        epoll->tail->head = epoll->head;
    }
    if (epoll->mqttwilltopiclen > 0 && epoll->mqttwillmsglen > 0) {
        PublishData(epoll->mqttwilltopic, epoll->mqttwilltopiclen, epoll->mqttwillmsg, epoll->mqttwillmsglen);
    }
}

void CheckMqttClients () { // 检查mqtt心跳
    EPOLL *epoll = epollhead;
    while (epoll != NULL) {
        EPOLL *tmp = epoll;
        epoll = epoll->tail;
        if (tmp->mqttstate == 1 && tmp->defaultkeepalive > 0) {
            tmp->keepalive--;
            if (tmp->keepalive == 0) {
                Epoll_Delete(tmp);
            }
        }
    }
}

#define MINPACKAGESIZE   32

static int CreateMqttBuffer (EPOLL *epoll, uint8_t *buff, uint32_t packagelen, uint32_t len) {
    uint8_t *package = (uint8_t*)smalloc(packagelen, __FILE__, __LINE__);
    if (package == NULL) {
        Epoll_Delete(epoll);
        return -1;
    }
    memcpy(package, buff, len);
    epoll->mqttpackage = package;
    epoll->mqttpackagecap = packagelen;
    epoll->mqttpackagelen = packagelen;
    epoll->mqttuselen = len;
    return 0;
}

static int GetMqttLength (uint8_t *buff, uint64_t len, uint32_t *packagelen, uint32_t *offset) {
    if (len < 2) {
        return -1;
    }
    if ((buff[1] & 0x80) != 0x00) {
        if (len < 3) {
            return -2;
        }
        if ((buff[2] & 0x80) != 0x00) {
            if (len < 4) {
                return -3;
            }
            if ((buff[3] & 0x80) != 0x00) {
                *packagelen = (((uint32_t)(buff[4] & 0x7f) << 21) | ((uint32_t)(buff[3] & 0x7f) << 14) | ((uint32_t)(buff[2] & 0x7f) << 7) | (uint32_t)(buff[1] & 0x7f)) + 5;
                *offset = 5;
            } else {
                *packagelen = (((uint32_t)(buff[3] & 0x7f) << 14) | ((uint32_t)(buff[2] & 0x7f) << 7) | (uint32_t)(buff[1] & 0x7f)) + 4;
                *offset = 4;
            }
        } else {
            *packagelen = (((uint32_t)(buff[2] & 0x7f) << 7) | (uint32_t)(buff[1] & 0x7f)) + 3;
            *offset = 3;
        }
    } else {
        *packagelen = (uint32_t)(buff[1] & 0x7f) + 2;
        *offset = 2;
    }
    return 0;
}

void HandleMqttClientRequest (EPOLL *epoll, uint8_t *buff, uint64_t len) {
    if (epoll->mqttpackagelen) {
        if (epoll->mqttuselen + len > epoll->mqttpackagecap) {
            uint32_t packagecap = epoll->mqttuselen + len;
            uint8_t *package = (uint8_t*)smalloc(packagecap, __FILE__, __LINE__);
            if (package == NULL) {
                Epoll_Delete(epoll);
                return;
            }
            memcpy(package, epoll->mqttpackage, epoll->mqttuselen);
            sfree(epoll->mqttpackage, __FILE__, __LINE__);
            epoll->mqttpackage = package;
            epoll->mqttpackagecap = packagecap;
        }
        memcpy(epoll->mqttpackage + epoll->mqttuselen, buff, len);
        epoll->mqttuselen += len;
        if (epoll->mqttuselen < epoll->mqttpackagelen) {
            return;
        }
        memcpy(buff, epoll->mqttpackage, epoll->mqttuselen);
        len = epoll->mqttuselen;
        sfree(epoll->mqttpackage, __FILE__, __LINE__);
        epoll->mqttpackagecap = 0;
        epoll->mqttpackagelen = 0;
        epoll->mqttuselen = 0;
    }
    uint32_t packagelen;
    uint32_t offset;
LOOP:
    if (GetMqttLength(buff, len, &packagelen, &offset)) {
        if (CreateMqttBuffer(epoll, buff, MINPACKAGESIZE, len)) {
            Epoll_Delete(epoll);
        }
        return;
    }
    if (packagelen > len) { // 数据并未获取完毕，需要创建缓存并反复拉取数据
        if (CreateMqttBuffer(epoll, buff, packagelen, len)) {
            Epoll_Delete(epoll);
        }
        return;
    }
    if (epoll->mqttstate) {
        epoll->keepalive = epoll->defaultkeepalive;
        uint8_t type = buff[0] & 0xf0;
        if (type == PUBLISH) {
            if (packagelen < offset + 2) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return;
            }
            uint16_t topiclen = ((uint16_t)buff[offset] << 8) + (uint16_t)buff[offset+1];
            offset += 2;
            uint8_t *topic = buff + offset;
            offset += topiclen;
            if ((buff[0] & 0x06) != 0x00) { // qos不为0时，存在报文标识符。
                if (epoll->mqttversion == 5) {
                    uint8_t puback[6] = { PUBACK, 0x04, buff[offset], buff[offset+1], 0x00, 0x00 };
                    epoll->write(epoll, puback, sizeof(puback));
                } else {
                    uint8_t puback[4] = { PUBACK, 0x02, buff[offset], buff[offset+1] };
                    epoll->write(epoll, puback, sizeof(puback));
                }
                offset += 2;
            }
            if (epoll->mqttversion == 5) {
                if (packagelen < offset+1) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    return;
                }
                uint8_t pubPropLen = buff[offset];
                offset += 1;
                if (packagelen < offset+pubPropLen) { // 直接跳过mqttv5新属性，PUBLISH Properties
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    return;
                }
                offset += pubPropLen;
            }
            uint8_t *b, *bv5, *btemp; // btemp是用于最后释放通过malloc创建出的新的publishdata来保存的指针，当mqttv5版本时指向b，mqtt3.1.1版本时指向bv5
            uint32_t blen, bv5len;
            if (epoll->mqttversion == 5) {
                btemp = CreatePublishData(topic, topiclen, buff+offset, packagelen-offset, 4, &blen);
                b = btemp;
                bv5 = buff;
                bv5len = packagelen;
            } else {
                b = buff;
                blen = packagelen;
                btemp = CreatePublishData(topic, topiclen, buff+offset, packagelen-offset, 5, &bv5len);
                bv5 = btemp;
            }
            SendToClient(b, blen, bv5, bv5len, topic, topiclen);
            sfree(btemp, __FILE__, __LINE__);
        } else if (type == PUBACK) {
            return;
        } else if (type == PUBREC || type == PUBREL || type == PUBCOMP) {
            Epoll_Delete(epoll);
            return;
        } else if (type == SUBSCRIBE) {
            if ((buff[0] & 0x0f) != 0x02) {
                Epoll_Delete(epoll);
                return;
            }
            if (packagelen < offset + 2) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return;
            }
            uint8_t suback[1024];
            uint16_t ackoffset;
            if (epoll->mqttversion == 5) {
                suback[0] = SUBACK;
                suback[1] = 0x03;
                suback[2] = buff[offset];
                suback[3] = buff[offset+1];
                suback[4] = 0x00;
                ackoffset = 5;
            } else {
                suback[0] = SUBACK;
                suback[1] = 0x02;
                suback[2] = buff[offset];
                suback[3] = buff[offset+1];
                ackoffset = 4;
            }
            offset += 2;
            if (epoll->mqttversion == 5) {
                if (packagelen < offset+1) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    return;
                }
                uint8_t subscribePropLen = buff[offset];
                offset += 1;
                if (packagelen < offset+subscribePropLen) { // 直接跳过mqttv5新属性，SUBSCRIBE Properties
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    return;
                }
                offset += subscribePropLen;
            }
            while (offset < packagelen) {
                if (packagelen < offset + 2) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return;
                }
                uint16_t topiclen = ((uint16_t)buff[offset] << 8) + (uint16_t)buff[offset+1];
                offset += 2;
                if (packagelen < offset + topiclen) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return;
                }
                uint8_t *topic = buff + offset;
                offset += topiclen;
                if (packagelen < offset + 1) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return;
                }
                if (buff[offset] != 0x00 && buff[offset] != 0x01 && buff[offset] != 0x02) {
                    printf("subscribe qos error, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return;
                }
                offset += 1;
                struct SubScribeList *sbbl = epoll->sbbl;
                while (sbbl != NULL) {
                    struct TopicList *topiclist = sbbl->topiclist;
                    if (topiclist->topiclen == topiclen && !memcmp(topiclist->topic, topic, topiclen)) {
                        break;
                    }
                    sbbl = sbbl->tail;
                }
                if (sbbl == NULL) {
                    struct TopicList *topiclist = topiclisthead;
                    while (topiclist != NULL) {
                        if (topiclist->topiclen == topiclen && !memcmp(topiclist->topic, topic, topiclen)) {
                            break;
                        }
                        topiclist = topiclist->tail;
                    }
                    if (topiclist == NULL) {
                        topiclist = (struct TopicList*)smalloc(sizeof(struct TopicList), __FILE__, __LINE__);
                        if (topiclist == NULL) {
                            Epoll_Delete(epoll);
                            return;
                        }
                        uint8_t *t = (uint8_t*)smalloc(topiclen, __FILE__, __LINE__);
                        if (t == NULL) {
                            Epoll_Delete(epoll);
                            return;
                        }
                        memcpy(t, topic, topiclen);
                        topiclist->topic = t;
                        topiclist->topiclen = topiclen;
                        topiclist->tte = NULL;
                        topiclist->head = NULL;
                        topiclist->tail = topiclisthead;
                        if (topiclisthead) {
                            topiclisthead->head = topiclist;
                        }
                        topiclisthead = topiclist;
                    }
                    struct TopicToEpoll *tte = topiclist->tte;
                    while (tte != NULL) {
                        if (tte->epoll == epoll) {
                            break;
                        }
                        tte = tte->tail;
                    }
                    if (tte == NULL) {
                        tte = (struct TopicToEpoll*)smalloc(sizeof(struct TopicToEpoll), __FILE__, __LINE__);
                        if (tte == NULL) {
                            Epoll_Delete(epoll);
                            return;
                        }
                        tte->epoll = epoll;
                        tte->head = NULL;
                        tte->tail = topiclist->tte;
                        if (topiclist->tte) {
                            topiclist->tte->head = tte;
                        }
                        topiclist->tte = tte;
                    }
                    sbbl = (struct SubScribeList*)smalloc(sizeof(struct SubScribeList), __FILE__, __LINE__);
                    if (sbbl == NULL) {
                        Epoll_Delete(epoll);
                        return;
                    }
                    sbbl->topiclist = topiclist;
                    sbbl->head = NULL;
                    sbbl->tail = epoll->sbbl;
                    if (epoll->sbbl) {
                        epoll->sbbl->head = sbbl;
                    }
                    epoll->sbbl = sbbl;
                }
                ++suback[1];
                suback[ackoffset] = 0x00;
                ++ackoffset;
            }
            epoll->write(epoll, suback, suback[1]+2);
        } else if (type == UNSUBSCRIBE) {
            if ((buff[0] & 0x0f) != 0x02) {
                // printf("in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return;
            }
            if (packagelen < offset + 2) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return;
            }
            uint8_t unsuback[5];
            if (epoll->mqttversion == 5) {
                unsuback[0] = UNSUBACK;
                unsuback[1] = 0x03;
                unsuback[2] = buff[offset];
                unsuback[3] = buff[offset+1];
                unsuback[4] = 0x00;
            } else {
                unsuback[0] = UNSUBACK;
                unsuback[1] = 0x02;
                unsuback[2] = buff[offset];
                unsuback[3] = buff[offset+1];
            }
            offset += 2;
            if (epoll->mqttversion == 5) {
                if (packagelen < offset+1) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    return;
                }
                uint8_t unSubscribePropLen = buff[offset];
                offset += 1;
                if (packagelen < offset+unSubscribePropLen) { // 直接跳过mqttv5新属性，UNSUBSCRIBE Properties
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    return;
                }
                offset += unSubscribePropLen;
            }
            while (offset < packagelen) {
                if (packagelen < offset + 2) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return;
                }
                uint16_t topiclen = ((uint16_t)buff[offset] << 8) + (uint16_t)buff[offset+1];
                offset += 2;
                if (packagelen < offset + topiclen) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return;
                }
                uint8_t *topic = buff + offset;
                struct SubScribeList *sbbl = epoll->sbbl;
                while (sbbl != NULL) {
                    if (sbbl->topiclist->topiclen == topiclen && !memcmp(sbbl->topiclist->topic, topic, topiclen)) {
                        break;
                    }
                    sbbl = sbbl->tail;
                }
                if (sbbl != NULL) {
                    UnSubScribeFunc(epoll, sbbl);
                }
                offset += topiclen;
            }
            epoll->write(epoll, unsuback, unsuback[1]+2);
        } else if (type == PINGREQ) {
            if (buff[1] != 0x00 || packagelen != 2) {
                // printf("in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return;
            }
            epoll->write(epoll, pingresp, sizeof(pingresp));
        } else if (type == DISCONNECT) {
            // printf("in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return;
        } else {
            printf("mqtt cmd:0x%02x, in %s, at %d\n", type, __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return;
        }
    } else {
        if ((buff[0] & 0xf0) != CONNECT) {
            if (epoll->mqttversion == 5) {
                epoll->write(epoll, connnologinv5, sizeof(connnologinv5));
            } else {
                epoll->write(epoll, connnologin, sizeof(connnologin));
            }
            Epoll_Delete(epoll);
            return;
        }
        if ((buff[0] & 0x0f) != 0x00) {
            if (epoll->mqttversion == 5) {
                epoll->write(epoll, connsererrv5, sizeof(connsererrv5));
            } else {
                epoll->write(epoll, connsererr, sizeof(connsererr));
            }
            Epoll_Delete(epoll);
            return;
        }
        if (packagelen < offset + 2) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return;
        }
        uint16_t protnamelen = ((uint16_t)buff[offset] << 8) + (uint16_t)buff[offset+1];
        offset += 2;
        if (packagelen < offset + protnamelen) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return;
        }
        // 这里可以读一下protname。
        offset += protnamelen;
        if (packagelen < offset + 1) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return;
        }
        if (buff[offset] != 0x03 && buff[offset] != 0x04 && buff[offset] != 0x05) { // 0x03为mqtt3.1, 0x04为mqtt3.1.1, 0x05为mqtt5
            printf("no support mqtt version:%d, in %s, at %d\n", buff[offset], __FILE__, __LINE__);
            if (epoll->mqttversion == 5) {
                epoll->write(epoll, connvererrv5, sizeof(connvererrv5));
            } else {
                epoll->write(epoll, connvererr, sizeof(connvererr));
            }
            Epoll_Delete(epoll);
            return;
        }
        epoll->mqttversion = buff[offset];
        offset += 1;
        if (packagelen < offset + 1) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return;
        }
        if ((buff[offset] & 0xc3) != 0xc2) { // 必须支持需要用户名，密码，清空session的模式。
            printf("no support mode:0x%02x, in %s, at %d\n", buff[offset], __FILE__, __LINE__);
            if (epoll->mqttversion == 5) {
                epoll->write(epoll, connautherrv5, sizeof(connautherrv5));
            } else {
                epoll->write(epoll, connsererr, sizeof(connsererr));
            }
            Epoll_Delete(epoll);
            return;
        }
        uint8_t needwill = buff[offset] & 0x04;
        offset += 1;
        if (packagelen < offset + 2) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return;
        }
        epoll->defaultkeepalive = 1.5 * (((uint16_t)buff[offset] << 8) + (uint16_t)buff[offset+1]);
        epoll->keepalive = epoll->defaultkeepalive;
        offset += 2;
        if (epoll->mqttversion == 5) {
            if (packagelen < offset+1) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return;
            }
            uint8_t connPropLen = buff[offset];
            offset += 1;
            if (packagelen < offset+connPropLen) { // 直接跳过mqttv5新属性，CONNECT Properties
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return;
            }
            offset += connPropLen;
        }
        if (packagelen < offset + 2) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return;
        }
        uint16_t clientidlen = ((uint16_t)buff[offset] << 8) + (uint16_t)buff[offset+1];
        offset += 2;
        if (packagelen < offset + clientidlen) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return;
        }
        uint8_t *clientid = buff + offset;
        offset += clientidlen;
        if (needwill) {
            if (epoll->mqttversion == 5) {
                if (packagelen < offset+1) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return;
                }
                uint8_t willPropLen = buff[offset];
                offset += 1;
                if (packagelen < offset+willPropLen) { // 直接跳过mqttv5新属性，Will Properties
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return;
                }
                offset += willPropLen;
            }
            if (packagelen < offset + 2) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return;
            }
            uint16_t willtopiclen = ((uint16_t)buff[offset] << 8) + (uint16_t)buff[offset+1];
            offset += 2;
            if (packagelen < offset + willtopiclen) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return;
            }
            uint8_t *willtopic = buff + offset;
            uint8_t *wt = (uint8_t*)smalloc(willtopiclen * sizeof(uint8_t), __FILE__, __LINE__);
            if (wt == NULL) {
                Epoll_Delete(epoll);
                return;
            }
            memcpy(wt, willtopic, willtopiclen);
            epoll->mqttwilltopic = wt;
            epoll->mqttwilltopiclen = willtopiclen;
            offset += willtopiclen;
            if (packagelen < offset + 2) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return;
            }
            uint16_t willmsglen = ((uint16_t)buff[offset] << 8) + (uint16_t)buff[offset+1];
            offset += 2;
            if (packagelen < offset + willmsglen) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return;
            }
            uint8_t *willmsg = buff + offset;
            uint8_t *wm = (uint8_t*)smalloc(willmsglen * sizeof(uint8_t), __FILE__, __LINE__);
            if (wm == NULL) {
                Epoll_Delete(epoll);
                return;
            }
            memcpy(wm, willmsg, willmsglen);
            epoll->mqttwillmsg = wm;
            epoll->mqttwillmsglen = willmsglen;
            offset += willmsglen;
        }
        if (packagelen < offset + 2) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return;
        }
        uint16_t userlen = ((uint16_t)buff[offset] << 8) + (uint16_t)buff[offset+1];
        offset += 2;
        if (packagelen < offset + userlen) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return;
        }
        uint8_t *user = buff + offset;
        struct ConfigData *configdata = GetConfig();
        if (userlen != configdata->mqttuserlen || strncmp(user, configdata->mqttuser, userlen)) { // 用户名错误
            if (epoll->mqttversion == 5) {
                epoll->write(epoll, connloginfailv5, sizeof(connloginfailv5));
            } else {
                epoll->write(epoll, connloginfail, sizeof(connloginfail));
            }
            Epoll_Delete(epoll);
            return;
        }
        offset += userlen;
        if (packagelen < offset + 2) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return;
        }
        uint16_t passlen = ((uint16_t)buff[offset] << 8) + (uint16_t)buff[offset+1];
        offset += 2;
        if (packagelen < offset + passlen) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return;
        }
        uint8_t *pass = buff + offset;
        if (configdata->mqttkeymode) {
            // 密码计算开始
            uint8_t *sha256signature = NULL;
            uint32_t timestamp = 0;
            uint32_t tsend = 0;
            for (int i = 0 ; i < passlen ; ++i) {
                if (pass[i] == '&') {
                    if (i + 65 != passlen) {
                        if (epoll->mqttversion == 5) {
                            epoll->write(epoll, connloginfailv5, sizeof(connloginfailv5));
                        } else {
                            epoll->write(epoll, connloginfail, sizeof(connloginfail));
                        }
                        Epoll_Delete(epoll);
                        return;
                    }
                    tsend = i;
                    sha256signature = pass + tsend + 1;
                    break;
                } else if (pass[i] < '0' && pass[i] > '9') {
                    if (epoll->mqttversion == 5) {
                        epoll->write(epoll, connloginfailv5, sizeof(connloginfailv5));
                    } else {
                        epoll->write(epoll, connloginfail, sizeof(connloginfail));
                    }
                    Epoll_Delete(epoll);
                    return;
                }
                timestamp = 10 * timestamp + pass[i] - '0';
            }
            if (sha256signature == NULL) {
                if (epoll->mqttversion == 5) {
                    epoll->write(epoll, connloginfailv5, sizeof(connloginfailv5));
                } else {
                    epoll->write(epoll, connloginfail, sizeof(connloginfail));
                }
                Epoll_Delete(epoll);
                return;
            }
            time_t t = time(NULL);
            if ((t + 600 < timestamp) || (timestamp + 600 < t)) { // 时间误差太大，直接舍弃
                if (epoll->mqttversion == 5) {
                    epoll->write(epoll, connloginfailv5, sizeof(connloginfailv5));
                } else {
                    epoll->write(epoll, connloginfail, sizeof(connloginfail));
                }
                Epoll_Delete(epoll);
                return;
            }
            uint8_t input[64];
            memcpy(input, pass, tsend);
            input[tsend] = '&';
            uint32_t keylen = 0;
            uint8_t *keyaddr = input + tsend + 1;
            while (configdata->mqttkey[keylen] != '\0') {
                keyaddr[keylen] = configdata->mqttkey[keylen];
                ++keylen;
            }
            uint8_t output[32];
            sha256_get(output, input, tsend + 1 + keylen);
            for (int i = 0 ; i < 32 ; ++i) {
                uint8_t a = sha256signature[2*i];
                uint8_t o;
                if ('a' <= a && a <= 'f') {
                    o = (a - 'a' + 10) << 4;
                } else if ('0' <= a && a <= '9') {
                    o = (a - '0') << 4;
                } else {
                    if (epoll->mqttversion == 5) {
                        epoll->write(epoll, connloginfailv5, sizeof(connloginfailv5));
                    } else {
                        epoll->write(epoll, connloginfail, sizeof(connloginfail));
                    }
                    Epoll_Delete(epoll);
                    return;
                }
                a = sha256signature[2*i+1];
                if ('a' <= a && a <= 'f') {
                    o |= a - 'a' + 10;
                } else if ('0' <= a && a <= '9') {
                    o |= a - '0';
                } else {
                    if (epoll->mqttversion == 5) {
                        epoll->write(epoll, connloginfailv5, sizeof(connloginfailv5));
                    } else {
                        epoll->write(epoll, connloginfail, sizeof(connloginfail));
                    }
                    Epoll_Delete(epoll);
                    return;
                }
                if (o != output[i]) {
                    if (epoll->mqttversion == 5) {
                        epoll->write(epoll, connloginfailv5, sizeof(connloginfailv5));
                    } else {
                        epoll->write(epoll, connloginfail, sizeof(connloginfail));
                    }
                    Epoll_Delete(epoll);
                    return;
                }
            }
            // 密码计算结束
        } else if (passlen != configdata->mqttkeylen || strncmp(pass, configdata->mqttkey, passlen)) {
            if (epoll->mqttversion == 5) {
                epoll->write(epoll, connloginfailv5, sizeof(connloginfailv5));
            } else {
                epoll->write(epoll, connloginfail, sizeof(connloginfail));
            }
            Epoll_Delete(epoll);
            return;
        }
        epoll->write(epoll, connsuccess, sizeof(connsuccess));
        epoll->mqttstate = 1;
        EPOLL *e = epollhead;
        while (e != NULL) {
            if (!memcmp(e->clientid, clientid, clientidlen)) {
                Epoll_Delete(e);
                break;
            }
            e = e->tail;
        }
        uint8_t *cid = (uint8_t*)smalloc(clientidlen, __FILE__, __LINE__);
        if (cid == NULL) {
            Epoll_Delete(epoll);
            return;
        }
        memcpy(cid, clientid, clientidlen);

        epoll->clientid = cid;
        epoll->clientidlen = clientidlen;
        epoll->head = NULL;
        epoll->tail = epollhead;
        if (epollhead) {
            epollhead->head = epoll;
        }
        epollhead = epoll;
    }
    if (packagelen == len) {
        return;
    }
    len -= packagelen;
    memcpy(buff, buff + packagelen, len);
    goto LOOP;
}
