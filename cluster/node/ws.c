#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include "event_poll.h"
#include "sha1.h"
#include "base64.h"
#include "config.h"
#include "mqtt.h"

#define ERRORPAGE       "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\nContent-Length: 84\r\nConnection: close\r\n\r\n<html><head><title>400 Bad Request</title></head><body>400 Bad Request</body></html>"
#define SUCCESSMSG      "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\nSec-WebSocket-Protocol: %s\r\n\r\n"
#define magic_String    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

static const unsigned char pong[] = {0x8a, 0x00};
static const unsigned char disconn[] = {0x88, 0x00};

struct HTTPPARAM {
    unsigned char *key;
    unsigned char *value;
};

static int ParseHttpHeader (char* str,
                                unsigned int str_size,
                                char **method,
                                char **path,
                                char **version,
                                struct HTTPPARAM *httpparam,
                                unsigned int *httpparam_size);

static int Ws_Delete_Connect (EPOLL *epoll) {
    write(epoll->fd, disconn, sizeof(disconn));
    remove_fd_from_poll(epoll);
}

static int Ws_Write_Connect (EPOLL *epoll, const unsigned char *data, unsigned long len) {
    unsigned char package[len+10];
    unsigned int packagelen;
    unsigned char *wsdata;
    package[0] = 0x82;
    if (len < 0x7e) {
        package[1] = len;
        wsdata = package + 2;
        packagelen = len + 2;
    } else if (len < 0x10000) {
        package[1] = 0x7e;
        package[2] = len >> 8;
        package[3] = len;
        wsdata = package + 4;
        packagelen = len + 4;
    } else {
        package[1] = 0x7f;
        package[2] = len >> 56;
        package[3] = len >> 48;
        package[4] = len >> 40;
        package[5] = len >> 32;
        package[6] = len >> 24;
        package[7] = len >> 16;
        package[8] = len >> 8;
        package[9] = len;
        wsdata = package + 10;
        packagelen = len + 10;
    }
    memcpy(wsdata, data, len);
    write(epoll->fd, package, packagelen);
}

static int Ws_Event_Handler (int event, EPOLL *epoll, unsigned char *buff) {
    if (event & (EPOLLERR|EPOLLHUP|EPOLLRDHUP)) { // 错误异常处理
        Ws_Delete_Connect(epoll);
        return 0;
    }
    ssize_t len = read(epoll->fd, buff, 512*1024);
    if (len < 0) {
        return -1;
    }
    if (!epoll->wsstate) {
        char *method, *path, *version;
        struct HTTPPARAM httpparam[30];
        unsigned int size = 30;
        int res = ParseHttpHeader(buff, len, &method, &path, &version, httpparam, &size);
        if (res) {
            printf("Parse Http Header fail, in %s, at %d\n", __FILE__, __LINE__);
            write(epoll->fd, ERRORPAGE, sizeof(ERRORPAGE));
            Ws_Delete_Connect(epoll);
            return -2;
        }
        int k = -1;
        int p = -1;
        for (int i = 0 ; i < size ; i++) {
            if (!strcmp(httpparam[i].key, "Sec-WebSocket-Key")) {
                k = i;
            }
            if (!strcmp(httpparam[i].key, "Sec-WebSocket-Protocol")) {
                p = i;
            }
            if (k != -1 && p != -1) {
                break;
            }
        }
        if (k == -1) {
            printf("not found Sec-WebSocket-Key, in %s, at %d\n", __FILE__, __LINE__);
            write(epoll->fd, ERRORPAGE, sizeof(ERRORPAGE));
            Ws_Delete_Connect(epoll);
            return -3;
        }
        if (p == -1) {
            printf("not found Sec-WebSocket-Protocol, in %s, at %d\n", __FILE__, __LINE__);
            write(epoll->fd, ERRORPAGE, sizeof(ERRORPAGE));
            Ws_Delete_Connect(epoll);
            return -4;
        }
        char input[64];
        int keylen = strlen(httpparam[k].value);
        memcpy(input, httpparam[k].value, keylen);
        memcpy(input + keylen, magic_String, sizeof(magic_String));
        char output[20];
        sha1(input, keylen + sizeof(magic_String)-1, output);
        char base64[30];
        int base64_len = 30;
        base64_encode(output, 20, base64, &base64_len);
        base64[base64_len] = '\0';
        char s[256];
        int res_len = sprintf(s, SUCCESSMSG, base64, httpparam[p].value);
        if (write(epoll->fd, s, res_len) < 0) {
            printf("write fail, in %s, at %d\n", __FILE__, __LINE__);
            Ws_Delete_Connect(epoll);
            return -5;
        }
        epoll->wsstate = 1;
        return 0;
    } else {
        if (epoll->wspackagelen) {
            memcpy(epoll->wspackage + epoll->wsuselen, buff, len);
            epoll->wsuselen += len;
            if (epoll->wsuselen < epoll->wspackagelen) {
                return 0;
            }
            memcpy(buff, epoll->wspackage, epoll->wsuselen);
            len = epoll->wsuselen;
            epoll->wsuselen = 0;
            epoll->wspackagelen = 0;
            free(epoll->wspackage);
            epoll->wspackage = NULL;
        }
        unsigned char *mask;
        unsigned char *data;
        unsigned int packagelen;
LOOP:
        if (len < 2) {
            printf("ws data so short, in %s, at %d\n", __FILE__, __LINE__);
            Ws_Delete_Connect(epoll);
            return -6;
        }
        unsigned int datalen = buff[1] & 0x7f;
        if (buff[1] & 0x80) {
            if (datalen < 0x7e) {
                mask = buff + 2;
                data = mask + 4;
                packagelen = datalen + 6;
            } else if (datalen == 0x7e) {
                if (len < 4) {
                    printf("ws data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Ws_Delete_Connect(epoll);
                    return -7;
                }
                mask = buff + 4;
                data = mask + 4;
                datalen = ((unsigned short)buff[2] << 8) | (unsigned short)buff[3];
                packagelen = datalen + 8;
            } else {
                if (len < 10) {
                    printf("ws data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Ws_Delete_Connect(epoll);
                    return -8;
                }
                mask = buff + 6;
                data = mask + 4;
                datalen = ((unsigned long)buff[2] << 56) | ((unsigned long)buff[3] << 48) | ((unsigned long)buff[4] << 40) | ((unsigned long)buff[5] << 32) | ((unsigned long)buff[6] << 24) | ((unsigned long)buff[7] << 16) | ((unsigned long)buff[8] << 8) | (unsigned long)buff[9];
                packagelen = datalen + 10;
            }
        } else {
            mask = NULL;
            if (datalen < 0x7e) {
                data = buff + 2;
                packagelen = datalen + 2;
            } else if (datalen == 0x7e) {
                if (len < 4) {
                    printf("ws data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Ws_Delete_Connect(epoll);
                    return -9;
                }
                data = buff + 4;
                datalen = ((unsigned short)buff[2] << 8) | (unsigned short)buff[3];
                packagelen = datalen + 4;
            } else {
                if (len < 10) {
                    printf("ws data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Ws_Delete_Connect(epoll);
                    return -10;
                }
                data = buff + 6;
                datalen = ((unsigned long)buff[2] << 56) | ((unsigned long)buff[3] << 48) | ((unsigned long)buff[4] << 40) | ((unsigned long)buff[5] << 32) | ((unsigned long)buff[6] << 24) | ((unsigned long)buff[7] << 16) | ((unsigned long)buff[8] << 8) | (unsigned long)buff[9];
                packagelen = datalen + 6;
            }
        }
        if (packagelen > len) { // 数据并未获取完毕，需要创建缓存并反复拉取数据
            epoll->wspackage = (unsigned char*)malloc(2*packagelen);
            if (epoll->wspackage == NULL) {
                printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                Ws_Delete_Connect(epoll);
                return -11;
            }
            memcpy(epoll->wspackage, buff, len);
            epoll->wspackagelen = packagelen;
            epoll->wsuselen = len;
            return 0;
        }
        if (mask) {
            for (unsigned int i = 0 ; i < datalen ; i++) {
                data[i] ^= mask[i & 0x03];
            }
        }
        if (buff[0] & 0x80) { // 最后一个数据包
            switch (buff[0] & 0x0f) {
                case 0x0:
                    printf("0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x, in %s, at %d\n", buff[0], buff[1], buff[2], buff[3], buff[4], buff[5], __FILE__, __LINE__);
                    break; // 中间数据包，不知道干嘛的
                case 0x1:
                    data[datalen] = '\0';
                    printf("%s\n", data);
                    break;
                case 0x2:
                    HandleMqttClientRequest(epoll, data, datalen);
                    break;
                case 0x8: Ws_Delete_Connect(epoll);break; // 断开连接
                case 0x9: write(epoll->fd, pong, sizeof(pong));break; // ping
            }
        } else {
            // 不是最后一个数据包，至今未出现过，可能要数据量超过4G才会出现。
            printf("0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x, in %s, at %d\n", buff[0], buff[1], buff[2], buff[3], buff[4], buff[5], __FILE__, __LINE__);
        }
        if (packagelen == len) {
            return 0;
        }
        len -= packagelen;
        memcpy(buff, buff + packagelen, len);
        goto LOOP;
    }
    return 0;
}

static int Ws_New_Connect (int event, EPOLL *e, unsigned char *buff) {
    struct sockaddr_in sin;
    socklen_t in_addr_len = sizeof(struct sockaddr_in);
    int fd = accept(e->fd, (struct sockaddr*)&sin, &in_addr_len);
    if (fd < 0) {
        printf("accept a new fd fail, in %s, at %d\n", __FILE__, __LINE__);
        return -1;
    }
    EPOLL *epoll = add_fd_to_poll(fd);
    if (epoll == NULL) {
        close(fd);
        return -2;
    }
    epoll->func = Ws_Event_Handler;
    epoll->write = Ws_Write_Connect;
    epoll->delete = Ws_Delete_Connect;
    epoll->mqttstate = 0;
    epoll->mqttpackage = NULL;
    epoll->mqttpackagelen = 0;
    epoll->mqttuselen = 0;
    epoll->wspackage = NULL;
    epoll->wspackagelen = 0;
    epoll->wsuselen = 0;
    epoll->wsstate = 0;
    return 0;
}

int Ws_Create () {
    struct ConfigData *configdata = GetConfig ();
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("create socket fail, in %s, at %d\n", __FILE__, __LINE__);
        return -1;
    }
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(struct sockaddr_in));
    sin.sin_family = AF_INET; // ipv4
    sin.sin_addr.s_addr = INADDR_ANY; // 任意ip
    sin.sin_port = htons(configdata->wsport);
    if (bind(fd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        printf("port %d bind fail, in %s, at %d\n", configdata->wsport, __FILE__, __LINE__);
        close(fd);
        return -2;
    }
    if (listen(fd, 16) < 0) {
        printf("listen port %d fail, in %s, at %d\n", configdata->wsport, __FILE__, __LINE__);
        close(fd);
        return -3;
    }
    EPOLL *epoll = add_fd_to_poll(fd);
    if (epoll == NULL) {
        close(fd);
        return -4;
    }
    epoll->fd = fd;
    epoll->func = Ws_New_Connect;
    return 0;
}

static int ParseHttpHeader (char* str,
                                unsigned int str_size,
                                char **method,
                                char **path,
                                char **version,
                                struct HTTPPARAM *httpparam,
                                unsigned int *httpparam_size) {
    unsigned int maxHttpParamNum = *httpparam_size;
    unsigned int httpParamNum = 0;
    unsigned char step = 0;
    for (int i = 0 ; i < str_size ; i++) {
        switch (step) {
            case 0: // 寻找mothod开始
                if (str[i] != ' ') {
                    *method = str + i;
                    step++;
                }
                break;
            case 1: // 寻找mothod的结束，path的开始
                if (str[i] == ' ') {
                    str[i] = '\0';
                    step++;
                }
                break;
            case 2: // 寻找path开始
                if (str[i] != ' ') {
                    *path = str + i;
                    step++;
                }
                break;
            case 3: // 寻找path结束
                if (str[i] == ' ') {
                    str[i] = '\0';
                    step++;
                }
                break;
            case 4: // 寻找version开始
                if (str[i] != ' ') {
                    *version = str + i;
                    step++;
                }
                break;
            case 5: // 寻找version结束
                if (str[i-1] == '\r' && str[i] == '\n') {
                    str[i-1] = '\0';
                    step = 7;
                } else if (str[i] == ' ') {
                    str[i] = '\0';
                    step++;
                }
                break;
            case 6: // 寻找换行
                if (str[i-1] == '\r' && str[i] == '\n') {
                    step = 7;
                }
            case 7: // 寻找PARAMKEY开始
                if ((str[i-3] == '\0' || str[i-3] == '\r') && str[i-2] == '\n' && str[i-1] == '\r' && str[i] == '\n') { // 正常退出处
                    *httpparam_size = httpParamNum;
                    return 0;
                }
                if (str[i] != ' ' && str[i] != '\r' && str[i] != '\n') {
                    if (httpParamNum == maxHttpParamNum) {
                        return -1;
                    }
                    httpparam[httpParamNum].key = str + i;
                    step++;
                }
                break;
            case 8: // 寻找PARAMKEY结束
                if (str[i] == ' ' || str[i] == ':') {
                    str[i] = '\0';
                    step++;
                }
                break;
            case 9: // 寻找PARAMVALUE开始
                if (str[i] != ' ' && str[i] != ':') {
                    httpparam[httpParamNum].value = str + i;
                    step++;
                }
                break;
            case 10: // 寻找PARAMKEY结束
                if (str[i-1] == '\r' && str[i] == '\n') {
                    str[i-1] = '\0';
                    httpParamNum++;
                    step = 7;
                } else if (str[i] == ' ') {
                    str[i] = '\0';
                    httpParamNum++;
                    step++;
                }
                break;
            case 11: // 寻找换行
                if (str[i-1] == '\r' && str[i] == '\n') {
                    step = 7;
                }
            default: break;
        }
    }
    return -2;
}
