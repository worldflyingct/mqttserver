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

#define ERRORPAGE       "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\nContent-Length: 84\r\nConnection: close\r\n\r\n<html><head><title>400 Bad Request</title></head><body>400 Bad Request</body></html>"
#define SUCCESSMSG      "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n"
#define magic_String    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

static const unsigned char pong[] = {0x8a, 0x00};

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
static int listenfd;
struct WS {
    int fd;
    unsigned char state; // 0为未注，1为注册
    struct EPOLL_PARAMS* epoll;
    unsigned char *package;
    unsigned int packagelen; // 当前包的理论大小
    unsigned int uselen; // 已经消耗的缓存
    struct WS *tail;
};
static struct WS *remainws = NULL;

static int Ws_Delete_Connect (struct WS *ws) {
    remove_fd_from_poll(ws->epoll);
    ws->tail = remainws;
    remainws = ws;
    close(ws->fd);
}

static int Ws_Event_Handler (int event, void *ptr) {
    static unsigned char buff[512*1024];
    struct WS *ws = ptr;
    if (event & (EPOLLERR|EPOLLHUP|EPOLLRDHUP)) { // 错误异常处理
        Ws_Delete_Connect(ws);
        return 0;
    }
    ssize_t len = read(ws->fd, buff, 32*1024);
    if (len < 0) {
        return 0;
    }
    if (!ws->state) {
        char *method, *path, *version;
        struct HTTPPARAM httpparam[30];
        unsigned int size = 30;
        int res = ParseHttpHeader(buff, len, &method, &path, &version, httpparam, &size);
        if (res) {
            printf("Parse Http Header fail, in %s, at %d\n", __FILE__, __LINE__);
            write(ws->fd, ERRORPAGE, sizeof(ERRORPAGE));
            Ws_Delete_Connect(ws);
            return -1;
        }
        int k = -1;
        for (int i = 0 ; i < size ; i++) {
            if (!strcmp(httpparam[i].key, "Sec-WebSocket-Key")) {
                k = i;
                break;
            }
        }
        if (k == -1) {
            printf("not found Sec-WebSocket-Key, in %s, at %d\n", __FILE__, __LINE__);
            write(ws->fd, ERRORPAGE, sizeof(ERRORPAGE));
            Ws_Delete_Connect(ws);
            return -2;
        }
        char input[64];
        int len1 = strlen(httpparam[k].value);
        memcpy(input, httpparam[k].value, len1);
        memcpy(input + len1, magic_String, sizeof(magic_String));
        char output[20];
        SHA1Context sha;
        SHA1Reset(&sha);
        SHA1Input(&sha, input, len1 + sizeof(magic_String)-1);
        SHA1Result(&sha, output);
        char base64[30];
        int base64_len = 30;
        base64_encode(output, 20, base64, &base64_len);
        base64[base64_len] = '\0';
        char s[128];
        int res_len = sprintf(s, SUCCESSMSG, base64);
        if (write(ws->fd, s, res_len) < 0) {
            printf("write fail, in %s, at %d\n", __FILE__, __LINE__);
            Ws_Delete_Connect(ws);
            return -3;
        }
        ws->state = 1;
        return 0;
    } else {
        if (ws->packagelen) {
            memcpy(ws->package + ws->uselen, buff, len);
            ws->uselen += len;
            if (ws->uselen < ws->packagelen) {
                return 0;
            }
            memcpy(buff, ws->package, ws->uselen);
            len = ws->uselen;
            ws->uselen = 0;
            ws->packagelen = 0;
            free(ws->package);
            ws->package = NULL;
        }
        while (1) {
            unsigned char *mask;
            unsigned char *data;
            unsigned int datalen = buff[1] & 0x7f;
            unsigned int packagelen;
            if (buff[1] & 0x80) {
                if (datalen < 0x7e) {
                    mask = buff + 2;
                    data = mask + 4;
                    packagelen = datalen + 6;
                } else if (datalen == 0x7e) {
                    mask = buff + 4;
                    data = mask + 4;
                    datalen = ((unsigned short)buff[2] << 8) | (unsigned short)buff[3];
                    packagelen = datalen + 8;
                } else {
                    mask = buff + 6;
                    data = mask + 4;
                    datalen = ((unsigned int)buff[2] << 24) | ((unsigned int)buff[3] << 16) | ((unsigned int)buff[4] << 8) | (unsigned int)buff[5];
                    packagelen = datalen + 10;
                }
            } else {
                mask = NULL;
                if (datalen < 0x7e) {
                    data = buff + 2;
                    packagelen = datalen + 2;
                } else if (datalen == 0x7e) {
                    data = buff + 4;
                    datalen = ((unsigned short)buff[2] << 8) | (unsigned short)buff[3];
                    packagelen = datalen + 4;
                } else {
                    data = buff + 6;
                    datalen = ((unsigned int)buff[2] << 24) | ((unsigned int)buff[3] << 16) | ((unsigned int)buff[4] << 8) | (unsigned int)buff[5];
                    packagelen = datalen + 6;
                }
            }
            if (packagelen > len) { // 数据并未获取完毕，需要创建缓存并反复拉取数据
                ws->package = (unsigned char*)malloc(2*packagelen);
                if (ws->package == NULL) {
                    printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                    Ws_Delete_Connect(ws);
                    return -4;
                }
                memcpy(ws->package, buff, len);
                ws->packagelen = packagelen;
                ws->uselen = len;
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
                    case 0x2: // 作为mqtt处理
                        break;
                    case 0x8: Ws_Delete_Connect(ws);break; // 断开连接
                    case 0x9: write(ws->fd, pong, sizeof(pong));break; // ping
                }
            } else {
                // 不是最后一个数据包，至今未出现过，可能要数据量超过4G才会出现。
                printf("0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x, in %s, at %d\n", buff[0], buff[1], buff[2], buff[3], buff[4], buff[5], __FILE__, __LINE__);
            }
            if (packagelen == len) {
                printf("in %s, at %d\n", __FILE__, __LINE__);
                return 0;
            }
            len -= packagelen;
            memcpy(buff, buff + packagelen, len);
        }
    }
    return 0;
}

static int Ws_New_Connect (int event, void *ptr) {
    struct sockaddr_in sin;
    socklen_t in_addr_len = sizeof(struct sockaddr_in);
    int fd = accept(listenfd, (struct sockaddr*)&sin, &in_addr_len);
    if (fd < 0) {
        printf("accept a new fd fail, in %s, at %d\n", __FILE__, __LINE__);
        return -1;
    }
    struct WS *ws;
    if (remainws) {
        ws = remainws;
        remainws = remainws->tail;
    } else {
        ws = (struct WS*)malloc(sizeof(struct WS));
        if (ws == NULL) {
            printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
            return -2;
        }
    }
    ws->state = 0;
    ws->fd = fd;
    struct EPOLL_PARAMS *epoll = add_fd_to_poll(fd, Ws_Event_Handler, ws);
    if (epoll == NULL) {
        printf("in %s, at %d\n", __FILE__, __LINE__);
        close(fd);
        ws->tail = remainws;
        remainws = ws;
        return -3;
    }
    ws->epoll = epoll;
    ws->package = NULL;
    ws->packagelen = 0;
    ws->uselen = 0;
    return 0;
}

int Ws_Create () {
    struct ConfigData *configdata = GetConfig ();
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        printf("create socket fail, in %s, at %d\n", __FILE__, __LINE__);
        return -1;
    }
    int on = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(struct sockaddr_in));
    sin.sin_family = AF_INET; // ipv4
    sin.sin_addr.s_addr = INADDR_ANY; // 任意ip
    sin.sin_port = htons(configdata->wsport);
    if (bind(listenfd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        printf("port %d bind fail, in %s, at %d\n", configdata->wsport, __FILE__, __LINE__);
        close(listenfd);
        return -2;
    }
    if (listen(listenfd, 16) < 0) {
        printf("listen port %d fail, in %s, at %d\n", configdata->wsport, __FILE__, __LINE__);
        close(listenfd);
        return -3;
    }
    struct EPOLL_PARAMS *epoll = add_fd_to_poll(listenfd, Ws_New_Connect, NULL);
    if (epoll == NULL) {
        close(listenfd);
        return -4;
    }
    return 0;
}

void Ws_Delete () {
    close(listenfd);
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
                if (str[i-3] == '\r' && str[i-2] == '\n' && str[i-1] == '\r' && str[i] == '\n') { // 正常退出处
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
