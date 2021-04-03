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

static const unsigned char connsuccess[]   = {CONNACK, 0x02, 0x00, 0x00};
static const unsigned char connvererr[]    = {CONNACK, 0x02, 0x00, 0x01};
static const unsigned char connsererr[]    = {CONNACK, 0x02, 0x00, 0x03};
static const unsigned char connloginfail[] = {CONNACK, 0x02, 0x00, 0x04};
static const unsigned char connnologin[]   = {CONNACK, 0x02, 0x00, 0x05};
static const unsigned char pingresp[]      = {PINGRESP, 0x00};

struct TopicList {
    unsigned char *topic;
    unsigned short topiclen;
    EPOLL *epoll;
    struct TopicList *head;
    struct TopicList *tail;
};
static struct TopicList *topiclisthead = NULL;
static EPOLL *epollhead = NULL;

void ShowClients () {
    EPOLL *epoll = epollhead;
    while (epoll != NULL) {
        unsigned char clientid[epoll->clientidlen+1];
        memcpy(clientid, epoll->clientid, epoll->clientidlen);
        clientid[epoll->clientidlen] = '\0';
        printf("clientid:%s topic:", clientid);
        struct SubScribeList *sbbl = epoll->sbbl;
        while (sbbl != NULL) {
            unsigned char topic[sbbl->topiclist->topiclen+1];
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
        unsigned char topic[topiclist->topiclen+1];
        memcpy(topic, topiclist->topic, topiclist->topiclen);
        topic[topiclist->topiclen] = '\0';
        printf("topic:%s clientid:", topic);
        EPOLL *epoll = topiclist->epoll;
        while (epoll != NULL) {
            unsigned char clientid[epoll->clientidlen+1];
            memcpy(clientid, epoll->clientid, epoll->clientidlen);
            clientid[epoll->clientidlen] = '\0';
            printf("%s ", clientid);
            epoll = epoll->ttail;
        }
        printf("\n");
        topiclist = topiclist->tail;
    }
}

static void SendToClient (unsigned char *buff, unsigned long packagelen, unsigned char *topic, unsigned long topiclen) {
    struct TopicList *topiclist = topiclisthead;
    while (topiclist != NULL) {
        if (topiclist->topiclen == topiclen && !memcmp(topiclist->topic, topic, topiclen)) {
            break;
        }
        topiclist = topiclist->tail;
    }
    if (topiclist != NULL) {
        EPOLL *epoll = topiclist->epoll;
        while (epoll != NULL) {
            epoll->write(epoll, buff, packagelen);
            epoll = epoll->ttail;
        }
    }
}

void PublishData (unsigned char *topic, unsigned long topiclen, unsigned char *msg, unsigned long msglen, unsigned char *buff) {
    unsigned long len = 2 + topiclen + msglen;
    unsigned long packagelen;
    unsigned long offset;
    if (len < 0x80) {
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
    SendToClient(buff, packagelen, topic, topiclen);
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
        sfree(topiclist->topic);
        sfree(topiclist);
    }
    sfree(sbbl);
}

int DeleteMqttClient (EPOLL *epoll, unsigned char *buff) {
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
        PublishData(epoll->mqttwilltopic, epoll->mqttwilltopiclen, epoll->mqttwillmsg, epoll->mqttwillmsglen, buff);
    }
}

static int GetMqttLength (unsigned char *buff, unsigned long len, unsigned long *packagelen, unsigned long *offset) {
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
        sfree(epoll->mqttpackage);
    }
    unsigned long packagelen;
    unsigned long offset;
LOOP:
    if (GetMqttLength(buff, len, &packagelen, &offset)) {
        Epoll_Delete(epoll);
        return -1;
    }
    if (packagelen > len) { // 数据并未获取完毕，需要创建缓存并反复拉取数据
        unsigned char *package = (unsigned char*)smalloc(2*packagelen);
        if (package == NULL) {
            printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -2;
        }
        memcpy(package, buff, len);
        epoll->mqttpackage = package;
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
        // 这里可以读一下protname。
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
        if ((buff[offset] & 0xc3) != 0xc2) { // 必须支持需要用户名，密码，清空session的模式。
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
            unsigned char *wt = (unsigned char*)smalloc(willtopiclen * sizeof(unsigned char));
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
            unsigned char *wm = (unsigned char*)smalloc(willmsglen * sizeof(unsigned char));
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
        if (memcmp(pass, configdata->mqttkey, passlen)) {
            unsigned char *sha256signature = NULL;
            unsigned int timestamp = 0;
            unsigned int tsend = 0;
            for (int i = 0 ; i < passlen ; i++) {
                if (pass[i] == '&') {
                    tsend = i;
                    sha256signature = pass + tsend + 1;
                    break;
                } else if (pass[i] < '0' && pass[i] > '9') {
                    return -25;
                }
                timestamp += 10 * (pass[i] - '0');
            }
            if (sha256signature == NULL) {
                return -26;
            }
            time_t t = time(NULL);
            if ((t + 1800 < timestamp) || (timestamp + 1800 < t)) { // 时间误差太大，直接舍弃
                epoll->write(epoll, connloginfail, sizeof(connloginfail));
                Epoll_Delete(epoll);
                return -27;
            }
            unsigned char input[64];
            memcpy(input, pass, tsend);
            input[tsend] = '&';
            unsigned int keylen = 0;
            unsigned char *keyaddr = input + tsend + 1;
            while (configdata->mqttkey[keylen] != '\0') {
                keyaddr[keylen] = configdata->mqttkey[keylen];
                keylen++;
            }
            unsigned char output[32];
            sha256(input, tsend + 1 + keylen, output);
            for (int i = 0 ; i < 32 ; i++) {
                unsigned char a = sha256signature[2*i];
                unsigned char o;
                if ('a' <= a && a <= 'f') {
                    o = (a - 'a' + 10) << 4;
                } else if ('0' <= a && a <= '9') {
                    o = (a - '0') << 4;
                } else {
                    epoll->write(epoll, connloginfail, sizeof(connloginfail));
                    Epoll_Delete(epoll);
                    return -28;
                }
                a = sha256signature[2*i+1];
                if ('a' <= a && a <= 'f') {
                    o |= a - 'a' + 10;
                } else if ('0' <= a && a <= '9') {
                    o |= a - '0';
                } else {
                    epoll->write(epoll, connloginfail, sizeof(connloginfail));
                    Epoll_Delete(epoll);
                    return -29;
                }
                if (o != output[i]) {
                    epoll->write(epoll, connloginfail, sizeof(connloginfail));
                    Epoll_Delete(epoll);
                    return -30;
                }
            }
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
        unsigned char *cid = (unsigned char*)smalloc(clientidlen);
        if (cid == NULL) {
            printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -31;
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
    } else {
        unsigned char type = buff[0] & 0xf0;
        if (type == PUBLISH) {
            if ((buff[0] & 0x0f) != 0x00) { // 本程序不处理dup，qos与retain不为0的报文。
                printf("publish dup,qos or retain is not 0, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -32;
            }
            if (packagelen < offset + 2) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -33;
            }
            unsigned short topiclen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
            offset += 2;
            unsigned char *topic = buff + offset;
            SendToClient(buff, packagelen, topic, topiclen);
        } else if (type == PUBACK || type == PUBREC || type == PUBREL || type == PUBCOMP) {
            Epoll_Delete(epoll);
            return -34;
        } else if (type == SUBSCRIBE) {
            if ((buff[0] & 0x0f) != 0x02) {
                Epoll_Delete(epoll);
                return -35;
            }
            if (packagelen < offset + 2) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -36;
            }
            unsigned char suback[1024] = { SUBACK, 0x02, buff[offset], buff[offset+1] };
            offset += 2;
            unsigned short ackoffset = 4;
            while (offset < packagelen) {
                if (packagelen < offset + 2) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return -37;
                }
                unsigned short topiclen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
                offset += 2;
                if (packagelen < offset + topiclen) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return -38;
                }
                unsigned char *topic = buff + offset;
                offset += topiclen;
                if (packagelen < offset + 1) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return -39;
                }
                if (buff[offset] != 0x00 && buff[offset] != 0x01 && buff[offset] != 0x02) {
                    Epoll_Delete(epoll);
                    return -40;
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
                    topiclist = (struct TopicList*)smalloc(sizeof(struct TopicList));
                    if (topiclist == NULL) {
                        printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                        Epoll_Delete(epoll);
                        return -41;
                    }
                    unsigned char *t = (unsigned char*)smalloc(topiclen);
                    if (t == NULL) {
                        printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                        Epoll_Delete(epoll);
                        return -42;
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
                struct SubScribeList *sbbl = epoll->sbbl;
                while (sbbl != NULL) {
                    if (sbbl->topiclist == topiclist) {
                        break;
                    }
                    sbbl = sbbl->tail;
                }
                if (sbbl == NULL) {
                    sbbl = (struct SubScribeList*)smalloc(sizeof(struct SubScribeList));
                    if (sbbl == NULL) {
                        printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                        Epoll_Delete(epoll);
                        return -43;
                    }
                    sbbl->topiclist = topiclist;
                    sbbl->head = NULL;
                    sbbl->tail = epoll->sbbl;
                    if (epoll->sbbl) {
                        epoll->sbbl->head = sbbl;
                    }
                    epoll->sbbl = sbbl;
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
                return -44;
            }
            if (packagelen < offset + 2) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -45;
            }
            unsigned char unsuback[4] = {UNSUBACK, 0x02, buff[offset], buff[offset+1]};
            offset += 2;
            while (offset < packagelen) {
                if (packagelen < offset + 2) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return -46;
                }
                unsigned short topiclen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
                offset += 2;
                if (packagelen < offset + topiclen) {
                    printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return -47;
                }
                unsigned char *topic = buff + offset;
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
            epoll->write(epoll, unsuback, 4);
        } else if (type == PINGREQ) {
            if (buff[1] != 0x00 || packagelen != 2) {
                // printf("in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return -48;
            }
            epoll->write(epoll, pingresp, sizeof(pingresp));
        } else if (type == DISCONNECT) {
            // printf("in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -49;
        } else {
            printf("mqtt cmd:0x%02x, in %s, at %d\n", type, __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return -50;
        }
    }
    if (packagelen == len) {
        return 0;
    }
    len -= packagelen;
    memcpy(buff, buff + packagelen, len);
    goto LOOP;
}
