#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "event_poll.h"
#include "sha256.h"

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

static const unsigned char pingresp[]   = {PINGRESP, 0x00};

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
        epoll->mqttpackage = NULL;
    }
    unsigned long packagelen;
    unsigned int offset;
LOOP:
    if (len < 2) {
        printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
        epoll->delete(epoll);
        return -1;
    }
    if ((buff[1] & 0x80) != 0x00) {
        if (len < 3) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            epoll->delete(epoll);
            return -2;
        }
        if ((buff[2] & 0x80) != 0x00) {
            if (len < 4) {
                printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
                epoll->delete(epoll);
                return -3;
            }
            if ((buff[3] & 0x80) != 0x00) {
                packagelen = 128 * 128 * 128 * (unsigned int)buff[4] + 128 * 128 * (unsigned int)(buff[3] & 0x7f) + 128 * (unsigned int)(buff[2] & 0x7f) + (unsigned int)(buff[1] & 0x7f) + 5;
                offset = 5;
            } else {
                packagelen = 128 * 128 * (unsigned int)buff[3] + 128 * (unsigned int)(buff[2] & 0x7f) + (unsigned int)(buff[1] & 0x7f) + 4;
                offset = 4;
            }
        } else {
            packagelen = 128 * (unsigned int)buff[2] + (unsigned int)(buff[1] & 0x7f) + 3;
            offset = 3;
        }
    } else {
        packagelen = (unsigned int)buff[1] + 2;
        offset = 2;
    }
    if (packagelen > len) { // 数据并未获取完毕，需要创建缓存并反复拉取数据
        epoll->mqttpackage = (unsigned char*)malloc(2*packagelen);
        if (epoll->mqttpackage == NULL) {
            printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
            epoll->delete(epoll);
            return -4;
        }
        memcpy(epoll->mqttpackage, buff, len);
        epoll->mqttpackagelen = packagelen;
        epoll->mqttuselen = len;
        return 0;
    }
    if (!epoll->mqttstate) {
        if ((buff[0] & 0xf0) != CONNECT) {
            epoll->write(epoll, connnologin, sizeof(connnologin));
            epoll->delete(epoll);
            return 0;
        }
        unsigned short protnamelen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];

        // unsigned char tmp = buff[2+offset+protnamelen];
        // buff[2+offset+protnamelen] = '\0';
        // printf("protocol name:%s\n", buff + offset + 2);
        // buff[2+offset+protnamelen] = tmp;

        offset += 2 + protnamelen;
        if (buff[offset] != 0x03 && buff[offset] != 0x04 && buff[offset] != 0x05) { // 0x03为mqtt3.1, 0x04为mqtt3.1.1, 0x05为mqtt5
            printf("no support mqtt version:%d, in %s, at %d\n", buff[offset], __FILE__, __LINE__);
            epoll->write(epoll, connvererr, sizeof(connvererr));
            epoll->delete(epoll);
            return 0;
        }
        offset += 1;
        if ((buff[offset] & 0xc2) != 0xc2) { // 必须支持需要用户名，密码，清空session的模式。
            printf("no support mode:0x%02x, in %s, at %d\n", buff[offset], __FILE__, __LINE__);
            epoll->write(epoll, connsererr, sizeof(connsererr));
            epoll->delete(epoll);
            return 0;
        }
        if (((buff[offset] & 0x04) != 0x00) && ((buff[offset] & 0x38) == 0x00)) { // 如果有遗嘱，遗嘱retain为0，遗嘱qos为0，清空session的模式。
            printf("no support mode:0x%02x, in %s, at %d\n", buff[offset], __FILE__, __LINE__);
            epoll->write(epoll, connsererr, sizeof(connsererr));
            epoll->delete(epoll);
            return 0;
        }
        unsigned char needwill = buff[offset] & 0x04;
        offset += 1;
        epoll->keepalive = 1.5 * (256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1]);
        epoll->deadline = epoll->keepalive;
        offset += 2;
        unsigned short clientidlen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
        offset += 2;
        unsigned char cliendid[clientidlen+1];
        memcpy(cliendid, buff+offset, clientidlen);
        cliendid[clientidlen] = '\0';
        offset += clientidlen;
        printf("cliendid:%s, in %s, at %d\n", cliendid, __FILE__, __LINE__);
        if (needwill) {
            unsigned short willtopiclen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
            offset += 2;
            unsigned char willtopic[willtopiclen+1];
            memcpy(willtopic, buff+offset, willtopiclen);
            willtopic[willtopiclen] = '\0';
            offset += willtopiclen;
            printf("willtopic:%s, in %s, at %d\n", willtopic, __FILE__, __LINE__);
            unsigned short willmsglen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
            offset += 2;
            unsigned char willmsg[willmsglen+1];
            memcpy(willmsg, buff+offset, willmsglen);
            willmsg[willmsglen] = '\0';
            offset += willmsglen;
            printf("willmsg:%s, in %s, at %d\n", willmsg, __FILE__, __LINE__);
        }
        unsigned short userlen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
        offset += 2;
        unsigned char user[userlen+1];
        memcpy(user, buff+offset, userlen);
        user[userlen] = '\0';
        offset += userlen;
        printf("user:%s, in %s, at %d\n", user, __FILE__, __LINE__);
        struct ConfigData *configdata = GetConfig();
        if (strcmp(user, configdata->mqttuser)) { // 用户名错误
            epoll->write(epoll, connloginfail, sizeof(connloginfail));
            epoll->delete(epoll);
            return 0;
        }
        unsigned short passlen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
        offset += 2;
        unsigned char pass[passlen+1];
        memcpy(pass, buff+offset, passlen);
        pass[passlen] = '\0';
        offset += passlen;
        printf("pass:%s, in %s, at %d\n", pass, __FILE__, __LINE__);
        // unsigned char *sha256signature;
        // unsigned int timestamp = 0;
        // for (int i = 0 ; i < passlen ; i++) {
        //     if (pass[i] == '&') {
        //         sha256signature = pass + 1;
        //         break;
        //     }
        //     timestamp += 10 * (pass[i] - '0');
        // }
        // time_t t = time(NULL);
        // if ((t + 1800 < timestamp) || (timestamp + 1800 < t)) { // 时间误差太大，直接舍弃
        //     epoll->write(epoll, connloginfail, sizeof(connloginfail));
        //     epoll->delete(epoll);
        //     return 0;
        // }
        // unsigned char input[userlen+10+16];
        // memcpy(input, user, userlen);
        // memcpy(input + userlen, pass, 10);
        // memcpy(input + userlen + 10, configdata->mqttkey, 16);
        // unsigned char output[32];
        // sha256(input, userlen+10+16, output);
        // if (memcmp(output, sha256signature, 32)) { // 签名错误
        //     epoll->write(epoll, connloginfail, sizeof(connloginfail));
        //     epoll->delete(epoll);
        //     return 0;
        // }
        epoll->write(epoll, connsuccess, sizeof(connsuccess));
        epoll->mqttstate = 1;
    } else {
        unsigned char type = buff[0] & 0xf0;
        if (type == PUBLISH) {
            if ((buff[0] & 0x0f) != 0x00) { // 本程序不处理dup，qos与retain不为0的报文。
                printf("publish dup,qos or retain is not 0, in %s, at %d\n", __FILE__, __LINE__);
                epoll->delete(epoll);
                return 0;
            }
            unsigned short topiclen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
            offset += 2;
            unsigned char topic[topiclen+1];
            memcpy(topic, buff+offset, topiclen);
            topic[topiclen] = '\0';
            offset += topiclen;
            // 如qos不为0时，这里将存在2bytes的报文标识符，由于本程序不支持qos不为0，因此不可能有报文标识符。
            unsigned char *payload = buff + offset;
            payload[packagelen] = '\0';
            printf("payload：%s, in %s, at %d\n", payload, __FILE__, __LINE__);
            epoll->deadline = epoll->keepalive;
        } else if (type == PUBACK || type == PUBREC || type == PUBREL || type == PUBCOMP) {
        } else if (type == SUBSCRIBE) {
            if ((buff[0] & 0x0f) != 0x02) {
                epoll->delete(epoll);
                return 0;
            }
            unsigned char suback[512] = { SUBACK, 0x02, buff[offset], buff[offset+1] };
            offset += 2;
            unsigned short ackoffset = 4;
            while (offset < packagelen) {
                unsigned short topiclen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
                offset += 2;
                unsigned char topic[topiclen+1];
                memcpy(topic, buff+offset, topiclen);
                topic[topiclen] = '\0';
                offset += topiclen;
                printf("topic:%s, in %s, at %d\n", topic, __FILE__, __LINE__);
                if (buff[offset] != 0x00 && buff[offset] != 0x01 && buff[offset] != 0x02) {
                    epoll->delete(epoll);
                    return 0;
                }
                offset += 1;
                suback[1]++;
                suback[ackoffset] = 0x00;
                ackoffset++;
            }
            epoll->write(epoll, suback, suback[1]+2);
            epoll->deadline = epoll->keepalive;
        } else if (type == UNSUBSCRIBE) {
            if ((buff[0] & 0x0f) != 0x02) {
                epoll->delete(epoll);
                return 0;
            }
            unsigned char unsuback[512] = {UNSUBACK, 0x02, buff[offset], buff[offset+1]};
            offset += 2;
            while (packagelen < offset) {
                unsigned short topiclen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
                offset += 2;
                unsigned char topic[topiclen+1];
                memcpy(topic, buff+offset, topiclen);
                topic[topiclen] = '\0';
                offset += topiclen;
                printf("topic: %s, in %s, at %d\n", topic, __FILE__, __LINE__);
            }
            epoll->write(epoll, unsuback, 4);
            epoll->deadline = epoll->keepalive;
        } else if (type == PINGREQ) {
            if (buff[1] != 0x00 || packagelen != 2) {
                epoll->delete(epoll);
                return 0;
            }
            epoll->write(epoll, pingresp, sizeof(pingresp));
            epoll->deadline = epoll->keepalive;
        } else if (type == DISCONNECT) {
            epoll->delete(epoll);
        } else {
            printf("mqtt cmd:0x%02x, in %s, at %d\n", type, __FILE__, __LINE__);
            epoll->delete(epoll);
        }
    }
    if (packagelen == len) {
        return 0;
    }
    len -= packagelen;
    memcpy(buff, buff + packagelen, len);
    goto LOOP;
}
