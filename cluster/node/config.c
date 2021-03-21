#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include "cJSON.h"
#include "config.h"

#define DEFAULTCONFIG   "{\r\n  \"tcpport\": 1883,\r\n  \"wsport\": 9001\r\n}"

struct ConfigData configdata;

struct ConfigData* InitConfig () {
    uint8_t buff[512];
    int fd = open("config.json", O_RDONLY);
    if (fd < 0) {
        configdata.tcpport = 1883;
        configdata.wsport = 9001;
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
    cJSON_Delete(json);
    return &configdata;
}


struct ConfigData* GetConfig () {
    return &configdata;
}
