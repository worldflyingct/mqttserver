#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "cJSON.h"
#include "config.h"

#define TCPPORT         "1883"
#define WSPORT          "80"
#define MQTTUSER        "5QdISjSg40jHFyzR"
#define MQTTKEY         "QLUL276kqXn55lBK"
#define DEFAULTCONFIG   "{\n" \
                        "  \"tcpport\": "TCPPORT",\n" \
                        "  \"wsport\": "WSPORT",\n" \
                        "  \"mqttuser\": \""MQTTUSER"\",\n" \
                        "  \"mqttkey\": \""MQTTKEY"\",\n" \
                        "  \"serverdomain\": \"localhost\",\n" \
                        "  \"serverport\": 80\r\n" \
                        "}"

struct ConfigData configdata;

struct ConfigData* InitConfig () {
    uint8_t buff[512];
    int fd = open("config.json", O_RDONLY);
    if (fd < 0) {
        configdata.tcpport = atoi(TCPPORT);
        configdata.wsport = atoi(WSPORT);
        strcpy(configdata.mqttuser, MQTTUSER);
        strcpy(configdata.mqttkey, MQTTKEY);
        fd = open("config.json", O_CREAT|O_WRONLY, 0777);
        if (fd < 0) {
            printf("init config file error.");
            return NULL;
        }
        if (write(fd, DEFAULTCONFIG, sizeof(DEFAULTCONFIG)-1) < 0) {
            close(fd);
            printf("init config file error.");
            return NULL;
        }
        close(fd);
        return &configdata;
    }
    ssize_t len = read(fd, buff, 512);
    if (len < 0) {
        printf("read config file error.");
        return NULL;
    }
    close(fd);
    buff[len] = '\0';
    cJSON *json = cJSON_Parse(buff);
    if (json == NULL) {
        printf("json parse fail.");
        return NULL;
    }
    cJSON *obj = cJSON_GetObjectItem(json, "tcpport");
    if (obj) {
        configdata.tcpport = obj->valueint;
    } else {
        configdata.tcpport = 0;
    }
    obj = cJSON_GetObjectItem(json, "wsport");
    if (obj) {
        configdata.wsport = obj->valueint;
    } else {
        configdata.wsport = 0;
    }
    obj = cJSON_GetObjectItem(json, "mqttuser");
    if (obj) {
        strncpy(configdata.mqttuser, obj->valuestring, 16);
    } else {
        memcpy(configdata.mqttuser, "QLUL276kqXn55lBK", 17);
    }
    obj = cJSON_GetObjectItem(json, "mqttkey");
    if (obj) {
        strncpy(configdata.mqttkey, obj->valuestring, 16);
    } else {
        memcpy(configdata.mqttkey, "QLUL276kqXn55lBK", 17);
    }
    obj = cJSON_GetObjectItem(json, "serverdomain");
    if (obj) {
        strncpy(configdata.serverdomain, obj->valuestring, 64);
    } else {
        memcpy(configdata.mqttkey, "localhost", 10);
    }
    configdata.regok = 0;
    cJSON_Delete(json);
    return &configdata;
}

struct ConfigData* GetConfig () {
    return &configdata;
}
