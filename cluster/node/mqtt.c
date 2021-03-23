#include "event_poll.h"

#define CONNECT    0x01

static const unsigned char disconnect[] = {0x20, 0x02, 0x00, 0x01};

struct MQTT {
    int fd;
    EVENT_FUNCTION *func;
    WRITE_FUNCTION *write;
    DELETE_FUNCTION *delete;
    unsigned char mqttstate; // 0为未注，1为注册
    unsigned char *mqttpackage;
    unsigned int mqttpackagelen; // 当前包的理论大小
    unsigned int mqttuselen; // 已经消耗的缓存
    unsigned short keepalive; // 无消息间隔时间，0为不检查
    unsigned short deadline; // 还剩多久断开连接，还原值为1.5倍的keepalive
    struct MQTT *head;
    struct MQTT *tail;
};

void HandleMqttClientRequest (void *ptr, unsigned char *buff, unsigned int len) {
    struct MQTT *mqtt = ptr;
    if (mqtt->mqttpackagelen) {
        memcpy(mqtt->mqttpackage + mqtt->mqttuselen, buff, len);
        mqtt->mqttuselen += len;
        if (mqtt->mqttuselen < mqtt->mqttpackagelen) {
            return 0;
        }
        memcpy(buff, mqtt->mqttpackage, mqtt->mqttuselen);
        len = mqtt->mqttuselen;
        mqtt->mqttuselen = 0;
        mqtt->mqttpackagelen = 0;
        free(mqtt->mqttpackage);
        mqtt->mqttpackage = NULL;
    }
    while (1) {
        if (len < 5) {
            printf("mqtt data so short, in %s, at %d\n", __FILE__, __LINE__);
            return 0;
        }
        unsigned int packagelen, offset;
        if ((buff[1] & 0x80) != 0x00) {
            if ((buff[2] & 0x80) != 0x00) {
                if ((buff[3] & 0x80) != 0x00) {
                    packagelen = 128 * 128 * 128 * (unsigned int)buff[4] + 128 * 128 * (unsigned int)(buff[3] & 0x7f) + 128 * (unsigned int)(buff[2] & 0x7f) + (unsigned int)(buff[1] & 0x7f) + 5;
                    offset = 5;
                } else {
                    packagelen = 128 * 128 * (unsigned int)buff[3] + 128 * (unsigned int)(buff[2] & 0x7f) + (unsigned int)(buff[1] & 0x7f) + 4;
                    offset = 4
                }
            } else {
                packagelen = 128 * (unsigned int)buff[2] + (unsigned int)(buff[1] & 0x7f) + 3;
                offset = 3
            }
        } else {
            packagelen = (unsigned int)buff[1] + 2;
            offset = 2
        }
        if (packagelen > len) { // 数据并未获取完毕，需要创建缓存并反复拉取数据
            mqtt->mqttpackage = (unsigned char*)malloc(2*packagelen);
            if (mqtt->mqttpackage == NULL) {
                printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                mqtt->delete()
                return -4;
            }
            memcpy(mqtt->mqttpackage, buff, len);
            mqtt->mqttpackagelen = packagelen;
            mqtt->mqttuselen = len;
            return 0;
        }
        if (!mqtt->mqttstate) {
            if ((buff[0] & 0xf0) != CONNECT) {
                mqtt->write(mqtt, disconnect, sizeof(disconnect));
                mqtt->delete(mqtt);
                return 0;
            }
            unsigned short protnamelen = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
            unsigned char tmp = buff[2+offset+protnamelen];
            buff[2+offset+protnamelen] = '\0';
            printf("protocol name:%s\n", buff + offset + 2);
            buff[2+offset+protnamelen] = tmp;
            offset += 2 + protnamelen;
            if (buff[offset] != 0x03 && buff[offset] != 0x04 && buff[offset] != 0x05) { // 0x03为mqtt3.1, 0x04为mqtt3.1.1, 0x05为mqtt5
                printf("no support mqtt version:%d\n", buff[offset])
                mqtt->write(mqtt, disconnect, sizeof(disconnect));
                mqtt->delete(mqtt);
                return 0;
            }
            offset += 1;
            if (buff[offset] != 0xc4) { // 仅仅支持需要用户名，密码，需要遗嘱，遗嘱retain为0，遗嘱qos为0，清空session的模式，自己使用，任性点。
                printf("no support mode:0x%02x\n", buff[offset]);
                mqtt->write(mqtt, disconnect, sizeof(disconnect));
                mqtt->delete(mqtt);
                return
            }
            offset += 1;
            mqtt->keepalive = 256 * (unsigned short)buff[offset] + (unsigned short)buff[offset+1];
            offset += 2;

        } else {
        }
    }
}
