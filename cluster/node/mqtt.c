#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "event_poll.h"
#include "mqtt.h"
#include "config.h"
#include "sha256.h"

static const unsigned char connsuccess[]   = {CONNACK, 0x02, 0x00, 0x00};
static const unsigned char connvererr[]    = {CONNACK, 0x02, 0x00, 0x01};
static const unsigned char connsererr[]    = {CONNACK, 0x02, 0x00, 0x03};
static const unsigned char connloginfail[] = {CONNACK, 0x02, 0x00, 0x04};
static const unsigned char connnologin[]   = {CONNACK, 0x02, 0x00, 0x05};
static const unsigned char pingresp[]   = {PINGRESP, 0x00};

struct TopicList {
    unsigned char *topic;
    unsigned short topiclen;
    EPOLL *epoll;
    struct TopicList *head;
    struct TopicList *tail;
};
struct TopicList *topiclisthead = NULL;
EPOLL *epollhead = NULL;

void ShowClients () {
    EPOLL *epoll = epollhead;
    while (epoll != NULL) {
        unsigned char clientid[epoll->clientidlen+1];
        memcpy(clientid, epoll->clientid, epoll->clientidlen);
        clientid[epoll->clientidlen] = '\0';
        printf("clientid:%s, %d\n", clientid, epoll->clientid);
        epoll = epoll->tail;
    }
}

void PublishData (unsigned char *topic, unsigned long topiclen, unsigned char *msg, unsigned long msglen, unsigned char *buff) {
    unsigned long len = 2 + topiclen + msglen;
    unsigned char packagelen;
    unsigned long offset;
    if (len < 127) {
        packagelen = len + 2;
        offset = 3;
    } else if (len < 0x4000) {
        packagelen = len + 3;
        offset = 4;
    } else if (len < 0x200000) {
        packagelen = len + 4;
        offset = 5;
    } else {
        packagelen = len + 5;
        offset = 6;
    }
    unsigned char n = 0;
    while (len > 0) {
        buff[n] |= 0x80;
        n++;
        buff[n] = len & 0x7f;
        len >>= 7;
    }
    buff[0] = PUBLISH;
    buff[offset] = topiclen >> 8;
    buff[offset+1] = topiclen;
    offset += 2;
    memcpy(buff + offset, topic, topiclen);
    offset += topiclen;
    memcpy(buff + offset, msg, msglen);
    struct TopicList *topiclist = topiclisthead;
    while (topiclist != NULL) {
        if (topiclist->topiclen == topiclen && !memcmp(topiclist->topic, topic, topiclen)) {
            break;
        }
        topiclist = topiclist->tail;
    }
    if (topiclist != NULL) {
        EPOLL *e = topiclist->epoll;
        while (e != NULL) {
            e->write(e, buff, packagelen);
            e = e->ttail;
        }
    }
}

void UnSubScribeFunc (EPOLL *epoll, struct SubScribeList *sbbl) {
    if (sbbl->head) {
        sbbl->head->tail = sbbl->tail;
    } else {
        epoll->subscribelist = sbbl->tail;
    }
    if (sbbl->tail) {
        sbbl->tail->head = sbbl->head;
    }
    struct TopicList *topiclist = sbbl->topiclist;
    if (epoll->thead) {
        epoll->thead->ttail = epoll->ttail;
    } else {
        topiclist->epoll = epoll->ttail;
    }
    if (epoll->ttail) {
        epoll->ttail->thead = epoll->thead;
    }
    if (topiclist->epoll == NULL) {
        if (topiclist->head) {
            topiclist->head->tail = topiclist->tail;
        } else {
            topiclisthead = topiclist->tail;
        }
        if (topiclist->tail) {
            topiclist->tail->head = topiclist->head;
        }
        free(topiclist->topic);
        free(topiclist);
    }
    free(sbbl);
}

int DeleteMqttClient (EPOLL *epoll, unsigned char *buff) {
    struct SubScribeList *sbbl = epoll->subscribelist;
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
        PublishData(epoll->mqttwilltopic, epoll->mqttwilltopiclen, epoll->mqttwillmsg, epoll->mqttwillmsglen, buff);
    }
}

int GetMqttLength (unsigned char *buff, unsigned long len, unsigned long *packagelen, unsigned long *offset) {
    if (len < 2) {
        printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
        return -1;
    }
    if ((buff[1] & 0x80) != 0x00) {
        if (len < 3) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            return -2;
        }
        if ((buff[2] & 0x80) != 0x00) {
            if (len < 4) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                return -3;
            }
            if ((buff[3] & 0x80) != 0x00) {
                *packagelen = 128 * 128 * 128 * (unsigned int)buff[4] + 128 * 128 * (unsigned int)(buff[3] & 0x7f) + 128 * (unsigned int)(buff[2] & 0x7f) + (unsigned int)(buff[1] & 0x7f) + 5;
                *offset = 5;
            } else {
                *packagelen = 128 * 128 * (unsigned int)buff[3] + 128 * (unsigned int)(buff[2] & 0x7f) + (unsigned int)(buff[1] & 0x7f) + 4;
                *offset = 4;
            }
        } else {
            *packagelen = 128 * (unsigned int)buff[2] + (unsigned int)(buff[1] & 0x7f) + 3;
            *offset = 3;
        }
    } else {
        *packagelen = (unsigned int)buff[1] + 2;
        *offset = 2;
    }
    return 0;
}

int HandleMqttClientRequest (EPOLL *epoll, unsigned char *buff, unsigned long len) {
    if (epoll->mqttpackagelen) {
        memcpy(epoll->mqttpackage + epoll->mqttuselen, buff, len);
        epoll->mqttuselen += len;
        if (epoll->mqttuselen < epoll->mqttpackagelen) {
            return 0;
        }
        memcpy(buff, epoll->mqttpackage, epoll->mqttuselen);
        len = epoll->mqttuselen;
        epoll->mqttuselen = 0;
        epoll->mqttpackagelen = 0;
        free(epoll->mqttpackage);
    }
    unsigned long packagelen;
    unsigned long offset;
LOOP:
    if (GetMqttLength(buff, len, &packagelen, &offset)) {
        Epoll_Delete(epoll);
        return -1;
    }
    if (packagelen > len) { // 数据并未获取完毕，需要创建缓存并反复拉取数据
        epoll->mqttpackage = (unsigned char*)malloc(2*packagelen);
        if (epoll->mqttpackage == NULL) {
            printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -2;
        }
        memcpy(epoll->mqttpackage, buff, len);
        epoll->mqttpackagelen = packagelen;
        epoll->mqttuselen = len;
        return -3;
    }
    if (!epoll->mqttstate) {
        if ((buff[0] & 0xf0) != CONNECT) {
            epoll->write(epoll, connnologin, sizeof(connnologin));
            Epoll_Delete(epoll);
            return -4;
        }
        if (packagelen < offset + 2) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -5;
        }
        unsigned short protnamelen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];

        if (packagelen < offset + 2 + protnamelen) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -6;
        }
        offset += 2 + protnamelen;

        if (packagelen < offset + 1) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -7;
        }
        if (buff[offset] != 0x03 && buff[offset] != 0x04 && buff[offset] != 0x05) { // 0x03为mqtt3.1, 0x04为mqtt3.1.1, 0x05为mqtt5
            printf("no support mqtt version:%d, in %s, at %d\n", buff[offset], __FILE__, __LINE__);
            epoll->write(epoll, connvererr, sizeof(connvererr));
            Epoll_Delete(epoll);
            return -8;
        }
        offset += 1;
        if (packagelen < offset + 1) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -9;
        }
        if ((buff[offset] & 0xc2) != 0xc2) { // 必须支持需要用户名，密码，清空session的模式。
            printf("no support mode:0x%02x, in %s, at %d\n", buff[offset], __FILE__, __LINE__);
            epoll->write(epoll, connsererr, sizeof(connsererr));
            Epoll_Delete(epoll);
            return -10;
        }
        if (((buff[offset] & 0x04) != 0x00) && ((buff[offset] & 0x38) == 0x00)) { // 如果有遗嘱，遗嘱retain为0，遗嘱qos为0，清空session的模式。
            printf("no support mode:0x%02x, in %s, at %d\n", buff[offset], __FILE__, __LINE__);
            epoll->write(epoll, connsererr, sizeof(connsererr));
            Epoll_Delete(epoll);
            return -11;
        }
        unsigned char needwill = buff[offset] & 0x04;
        offset += 1;
        if (packagelen < offset + 2) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -12;
        }
        epoll->keepalive = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
        offset += 2;
        if (packagelen < offset + 2) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -13;
        }
        unsigned short clientidlen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
        offset += 2;
        if (packagelen < offset + 2) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -14;
        }
        unsigned char *clientid = buff + offset;
        offset += clientidlen;
        if (needwill) {
            unsigned short willtopiclen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
            offset += 2;
            if (packagelen < offset + willtopiclen) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -15;
            }
            unsigned char *willtopic = buff + offset;
            unsigned char *wt = (unsigned char*)malloc(willtopiclen * sizeof(unsigned char));
            if (wt == NULL) {
                printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -16;
            }
            memcpy(wt, willtopic, willtopiclen);
            epoll->mqttwilltopic = wt;
            epoll->mqttwilltopiclen = willtopiclen;
            offset += willtopiclen;
            printf("willtopic:%s, in %s, at %d\n", willtopic, __FILE__, __LINE__);
            if (packagelen < offset + 2) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -17;
            }
            unsigned short willmsglen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
            offset += 2;
            if (packagelen < offset + willmsglen) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -18;
            }
            unsigned char *willmsg = buff + offset;
            unsigned char *wm = (unsigned char*)malloc(willmsglen * sizeof(unsigned char));
            if (wm == NULL) {
                printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -19;
            }
            memcpy(wm, willmsg, willmsglen);
            epoll->mqttwillmsg = wm;
            epoll->mqttwillmsglen = willmsglen;
            offset += willmsglen;
            if (packagelen < offset + 2) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -20;
            }
        }
        unsigned short userlen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
        offset += 2;
        if (packagelen < offset + userlen) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -21;
        }
        unsigned char *user = buff + offset;
        struct ConfigData *configdata = GetConfig();
        if (strncmp(user, configdata->mqttuser, userlen)) { // 用户名错误
            epoll->write(epoll, connloginfail, sizeof(connloginfail));
            Epoll_Delete(epoll);
            return -22;
        }
        offset += userlen;
        if (packagelen < offset + 2) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -23;
        }
        unsigned short passlen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
        offset += 2;
        if (packagelen < offset + passlen) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -24;
        }
        unsigned char *pass = buff + offset;
#ifdef RELEASE
        unsigned char *sha256signature;
        unsigned int timestamp = 0;
        for (int i = 0 ; i < passlen ; i++) {
            if (pass[i] == '&') {
                sha256signature = pass + 1;
                break;
            }
            timestamp += 10 * (pass[i] - '0');
        }
        time_t t = time(NULL);
        if ((t + 1800 < timestamp) || (timestamp + 1800 < t)) { // 时间误差太大，直接舍弃
            epoll->write(epoll, connloginfail, sizeof(connloginfail));
            Epoll_Delete(epoll);
            return -25;
        }
        unsigned char input[userlen+10+16];
        memcpy(input, user, userlen);
        memcpy(input + userlen, pass, 10);
        memcpy(input + userlen + 10, configdata->mqttkey, 16);
        unsigned char output[32];
        sha256(input, userlen+10+16, output);
        if (memcmp(output, sha256signature, 32)) { // 签名错误
            epoll->write(epoll, connloginfail, sizeof(connloginfail));
            Epoll_Delete(epoll);
            return -26;
        }
#else
        if (strncmp(pass, configdata->mqttkey, passlen)) {
            epoll->write(epoll, connloginfail, sizeof(connloginfail));
            Epoll_Delete(epoll);
            return -26;
        }
#endif
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
        unsigned char *c = (unsigned char*)malloc(clientidlen);
        if (c == NULL) {
            printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -27;
        }
        memcpy(c, clientid, clientidlen);

        epoll->clientid = c;
        epoll->clientidlen = clientidlen;
        epoll->head = NULL;
        epoll->tail = epollhead;
        if (epollhead) {
            epollhead->head = epoll;
        }
        epollhead = epoll;
    } else {
        unsigned char type = buff[0] & 0xf0;
        if (type == PUBLISH) {
            if ((buff[0] & 0x0f) != 0x00) { // 本程序不处理dup，qos与retain不为0的报文。
                printf("publish dup,qos or retain is not 0, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -28;
            }
            if (packagelen < offset + 2) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -29;
            }
            unsigned short topiclen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
            offset += 2;
            unsigned char *topic = buff + offset;
            struct TopicList *topiclist = topiclisthead;
            while (topiclist != NULL) {
                if (topiclist->topiclen == topiclen && !memcmp(topiclist->topic, topic, topiclen)) {
                    break;
                }
                topiclist = topiclist->tail;
            }
            if (topiclist != NULL) {
                EPOLL *e = topiclist->epoll;
                while (e != NULL) {
                    e->write(e, buff, packagelen);
                    e = e->ttail;
                }
            }
        } else if (type == PUBACK || type == PUBREC || type == PUBREL || type == PUBCOMP) {
        } else if (type == SUBSCRIBE) {
            if ((buff[0] & 0x0f) != 0x02) {
                Epoll_Delete(epoll);
                return -30;
            }
            if (packagelen < offset + 2) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -31;
            }
            unsigned char suback[1024] = { SUBACK, 0x02, buff[offset], buff[offset+1] };
            offset += 2;
            unsigned short ackoffset = 4;
            while (offset < packagelen) {
                if (packagelen < offset + 2) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return -32;
                }
                unsigned short topiclen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
                offset += 2;
                if (packagelen < offset + topiclen) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return -33;
                }
                unsigned char *topic = buff + offset;
                offset += topiclen;
                if (packagelen < offset + 1) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return -34;
                }
                if (buff[offset] != 0x00 && buff[offset] != 0x01 && buff[offset] != 0x02) {
                    Epoll_Delete(epoll);
                    return -35;
                }
                offset += 1;
                struct TopicList *topiclist = topiclisthead;
                while (topiclist != NULL) {
                    if (topiclist->topiclen == topiclen && !memcmp(topiclist->topic, topic, topiclen)) {
                        break;
                    }
                    topiclist = topiclist->tail;
                }
                if (topiclist == NULL) {
                    topiclist = (struct TopicList*)malloc(sizeof(struct TopicList));
                    if (topiclist == NULL) {
                        printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                        Epoll_Delete(epoll);
                        return -36;
                    }
                    unsigned char *t = (unsigned char*)malloc(topiclen);
                    if (t == NULL) {
                        printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                        Epoll_Delete(epoll);
                        return -37;
                    }
                    memcpy(t, topic, topiclen);
                    topiclist->topic = t;
                    topiclist->topiclen = topiclen;
                    topiclist->epoll = NULL;
                    topiclist->head = NULL;
                    topiclist->tail = topiclisthead;
                    if (topiclisthead) {
                        topiclisthead->head = topiclist;
                    }
                    topiclisthead = topiclist;
                }
                EPOLL *eobj = topiclist->epoll;
                while (eobj != NULL) {
                    if (eobj == epoll) {
                        break;
                    }
                    eobj = eobj->ttail;
                }
                if (eobj == NULL) {
                    epoll->thead = NULL;
                    epoll->ttail = topiclist->epoll;
                    if (topiclist->epoll) {
                        topiclist->epoll->thead = epoll;
                    }
                    topiclist->epoll = epoll;
                }
                struct SubScribeList *sbbl = epoll->subscribelist;
                while (sbbl != NULL) {
                    if (sbbl->topiclist == topiclist) {
                        break;
                    }
                    sbbl = sbbl->tail;
                }
                if (sbbl == NULL) {
                    sbbl = (struct SubScribeList*)malloc(sizeof(struct SubScribeList));
                    if (sbbl == NULL) {
                        printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                        Epoll_Delete(epoll);
                        return -38;
                    }
                    sbbl->topiclist = topiclist;
                    sbbl->head = NULL;
                    sbbl->tail = epoll->subscribelist;
                    if (epoll->subscribelist) {
                        epoll->subscribelist->head = sbbl;
                    }
                    epoll->subscribelist = sbbl;
                }
                suback[1]++;
                suback[ackoffset] = 0x00;
                ackoffset++;
            }
            epoll->write(epoll, suback, suback[1]+2);
        } else if (type == UNSUBSCRIBE) {
            if ((buff[0] & 0x0f) != 0x02) {
                // printf("in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -39;
            }
            if (packagelen < offset + 2) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -40;
            }
            unsigned char unsuback[1024] = {UNSUBACK, 0x02, buff[offset], buff[offset+1]};
            offset += 2;
            while (packagelen < offset) {
                if (packagelen < offset + 2) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return -41;
                }
                unsigned short topiclen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
                offset += 2;
                if (packagelen < offset + topiclen) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return -42;
                }
                unsigned char *topic = buff + offset;
                struct SubScribeList *sbbl = epoll->subscribelist;
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
            epoll->write(epoll, unsuback, 4);
        } else if (type == PINGREQ) {
            if (buff[1] != 0x00 || packagelen != 2) {
                // printf("in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -43;
            }
            epoll->write(epoll, pingresp, sizeof(pingresp));
        } else if (type == DISCONNECT) {
            // printf("in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -44;
        } else {
            printf("mqtt cmd:0x%02x, in %s, at %d\n", type, __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -45;
        }
    }
    if (packagelen == len) {
        return 0;
    }
    len -= packagelen;
    memcpy(buff, buff + packagelen, len);
    goto LOOP;
}
