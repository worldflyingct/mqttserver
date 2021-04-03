#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "cJSON.h"
#include "config.h"

#define TCPPORT         "1883"
#define TLSPORT         "1884"
#define WSPORT          "80"
#define WSSPORT         "443"
#define CRTPATH         "server.crt"
#define KEYPATH         "server.key"
#define MQTTUSER        "5QdISjSg40jHFyzR"
#define MQTTKEY         "QLUL276kqXn55lBK"
#define SERVERDOMAIN    "localhost"
#define SERVERPORT      "80"
#define DEFAULTCONFIG   "{\n" \
                        "  \"tcpport\": "TCPPORT",\n" \
                        "  \"tlsport\": "TLSPORT",\n" \
                        "  \"wsport\": "WSPORT",\n" \
                        "  \"wssport\": "WSSPORT",\n" \
                        "  \"crtpath\": \""CRTPATH"\",\n" \
                        "  \"keypath\": \""KEYPATH"\",\n" \
                        "  \"mqttuser\": \""MQTTUSER"\",\n" \
                        "  \"mqttkey\": \""MQTTKEY"\",\n" \
                        "  \"serverdomain\": \""SERVERDOMAIN"\",\n" \
                        "  \"serverport\": "SERVERPORT"\r\n" \
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
        strcpy(configdata.mqttkey, MQTTKEY);
        strcpy(configdata.serverdomain, SERVERDOMAIN);
        configdata.serverport = atoi(SERVERPORT);
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
        strncpy(configdata.mqttuser, obj->valuestring, 16);
    } else {
        memcpy(configdata.mqttuser, MQTTUSER, sizeof(MQTTUSER));
    }
    obj = cJSON_GetObjectItem(json, "mqttkey");
    if (obj) {
        strncpy(configdata.mqttkey, obj->valuestring, 16);
    } else {
        memcpy(configdata.mqttkey, MQTTKEY, sizeof(MQTTKEY));
    }
    obj = cJSON_GetObjectItem(json, "serverdomain");
    if (obj) {
        strncpy(configdata.serverdomain, obj->valuestring, 129);
    } else {
        memcpy(configdata.mqttkey, SERVERDOMAIN, sizeof(SERVERDOMAIN));
    }
    obj = cJSON_GetObjectItem(json, "serverport");
    if (obj) {
        configdata.serverport = obj->valueint;
    } else {
        configdata.serverport = 80;
    }
    cJSON_Delete(json);
    return &configdata;
}

struct ConfigData* GetConfig () {
    return &configdata;
}
