#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "cJSON.h"
#include "config.h"

#define TCPPORT             "1883"
#define TLSPORT             "0"
#define WSPORT              "80"
#define WSSPORT             "0"
#define CRTPATH             "server.crt"
#define KEYPATH             "server.key"
#define MQTTUSER            "admin"
#define MQTTKEY             "worldflying.cn"
#define MQTTKEYMODE         "0" // 0为一般模式，1为根据当前时间戳与sha256计算模式
#define DEFAULTCONFIG       "{\n" \
                            "  \"tcpport\": "TCPPORT",\n" \
                            "  \"tlsport\": "TLSPORT",\n" \
                            "  \"wsport\": "WSPORT",\n" \
                            "  \"wssport\": "WSSPORT",\n" \
                            "  \"crtpath\": \""CRTPATH"\",\n" \
                            "  \"keypath\": \""KEYPATH"\",\n" \
                            "  \"mqttuser\": \""MQTTUSER"\",\n" \
                            "  \"mqttkey\": \""MQTTKEY"\",\n" \
                            "  \"mqttkeymode\": "MQTTKEYMODE"\n" \
                            "}"

struct ConfigData configdata;

struct ConfigData* InitConfig () {
    uint8_t buff[1024];
    int fd = open("config.json", O_RDONLY);
    if (fd < 0) {
        configdata.tcpport = atoi(TCPPORT);
        configdata.tlsport = atoi(TLSPORT);
        configdata.wsport = atoi(WSPORT);
        configdata.wssport = atoi(WSSPORT);
        strcpy(configdata.crtpath, CRTPATH);
        strcpy(configdata.keypath, KEYPATH);
        strcpy(configdata.mqttuser, MQTTUSER);
        configdata.mqttuserlen = sizeof(MQTTUSER)-1;
        strcpy(configdata.mqttkey, MQTTKEY);
        configdata.mqttkeylen = sizeof(MQTTKEY)-1;
        configdata.mqttkeymode = atoi(MQTTKEYMODE);
        fd = open("config.json", O_CREAT|O_WRONLY, 0777);
        if (fd < 0) {
            printf("init config file error.");
            return NULL;
        }
        if (write(fd, DEFAULTCONFIG, sizeof(DEFAULTCONFIG)-1) < 0) {
            printf("init config file error.");
            close(fd);
            return NULL;
        }
        close(fd);
        return &configdata;
    }
    ssize_t len = read(fd, buff, 1024);
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
    obj = cJSON_GetObjectItem(json, "tlsport");
    if (obj) {
        configdata.tlsport = obj->valueint;
    } else {
        configdata.tlsport = 0;
    }
    obj = cJSON_GetObjectItem(json, "wsport");
    if (obj) {
        configdata.wsport = obj->valueint;
    } else {
        configdata.wsport = 0;
    }
    obj = cJSON_GetObjectItem(json, "wssport");
    if (obj) {
        configdata.wssport = obj->valueint;
    } else {
        configdata.wssport = 0;
    }
    obj = cJSON_GetObjectItem(json, "crtpath");
    if (obj) {
        strncpy(configdata.crtpath, obj->valuestring, 128);
    } else {
        memcpy(configdata.crtpath, CRTPATH, sizeof(CRTPATH));
    }
    obj = cJSON_GetObjectItem(json, "keypath");
    if (obj) {
        strncpy(configdata.keypath, obj->valuestring, 128);
    } else {
        memcpy(configdata.keypath, KEYPATH, sizeof(KEYPATH));
    }
    obj = cJSON_GetObjectItem(json, "mqttuser");
    if (obj) {
        strncpy(configdata.mqttuser, obj->valuestring, 32);
    } else {
        memcpy(configdata.mqttuser, MQTTUSER, sizeof(MQTTUSER));
    }
    configdata.mqttuserlen = strlen(configdata.mqttuser);
    obj = cJSON_GetObjectItem(json, "mqttkey");
    if (obj) {
        strncpy(configdata.mqttkey, obj->valuestring, 32);
    } else {
        memcpy(configdata.mqttkey, MQTTKEY, sizeof(MQTTKEY));
    }
    configdata.mqttkeylen = strlen(configdata.mqttkey);
    obj = cJSON_GetObjectItem(json, "mqttkeymode");
    if (obj) {
        configdata.mqttkeymode = obj->valueint;
    } else {
        configdata.mqttkeymode = 0;
    }
    cJSON_Delete(json);
    return &configdata;
}

struct ConfigData* GetConfig () {
    return &configdata;
}
