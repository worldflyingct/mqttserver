#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include "event_poll.h"
#include "sha1.h"
#include "base64.h"
#include "config.h"
#include "mqtt.h"
#include "smalloc.h"

#define ERRORPAGE       "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\nContent-Length: 84\r\nConnection: close\r\n\r\n<html><head><title>400 Bad Request</title></head><body>400 Bad Request</body></html>"
#define SUCCESSMSG      "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\nSec-WebSocket-Protocol: %s\r\n\r\n"
#define SUCCESSPAGE     "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 77\r\nConnection: close\r\n\r\n<html><head><title>Request Success</title></head><body>Success.</body></html>"
#define HTTPOKHEAD      "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s"
#define PAGE500         "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/html\r\nContent-Length: 104\r\nConnection: close\r\n\r\n<html><head><title>500 Internal Server Error</title></head><body>500 Internal Server Error</body></html>"
#define magic_String    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

static const unsigned char pong[] = {0x8a, 0x00};

static int ParseHttpHeader (char* str,
                                unsigned int str_size,
                                char **method,
                                char **path,
                                char **version,
                                struct HTTPPARAM *httpparam,
                                unsigned int *httpparam_size);

static void Ws_Write_Connect (EPOLL *epoll, const unsigned char *data, unsigned long len) {
    if (len < 0x7e) {
        unsigned char package[len+2];
        package[0] = 0x82;
        package[1] = len;
        memcpy(package + 2, data, len);
        Epoll_Write(epoll, package, len + 2);
    } else if (len < 0x10000) {
        unsigned char package[len+4];
        package[0] = 0x82;
        package[1] = 0x7e;
        package[2] = len >> 8;
        package[3] = len;
        memcpy(package + 4, data, len);
        Epoll_Write(epoll, package, len + 4);
    } else {
        unsigned char package[len+10];
        package[0] = 0x82;
        package[1] = 0x7f;
        package[2] = len >> 56;
        package[3] = len >> 48;
        package[4] = len >> 40;
        package[5] = len >> 32;
        package[6] = len >> 24;
        package[7] = len >> 16;
        package[8] = len >> 8;
        package[9] = len;
        memcpy(package + 10, data, len);
        Epoll_Write(epoll, package, len + 10);
    }
}

static void Ws_Read_Handler (EPOLL *epoll, unsigned char *buff) {
    ssize_t len;
    if (epoll->tls) {
        len = SSL_read(epoll->tls, buff, 512*1024);
    } else {
        len = read(epoll->fd, buff, 512*1024);
    }
    if (len < 0) {
        if (epoll->tls) {
            int errcode = SSL_get_error(epoll->tls, len);
            if (errcode == SSL_ERROR_WANT_WRITE) {
                epoll->tlsok = 0;
                mod_fd_at_poll(epoll, 1);
                epoll->writeenable = 0;
            }
        }
        return;
    } else if (epoll->wsstate) {
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        if (epoll->wspackagelen) {
            if (epoll->wsuselen + len > epoll->wspackagecap) {
                unsigned int packagecap = epoll->wsuselen + len;
                unsigned char *package = (unsigned char*)smalloc(packagecap);
                if (package == NULL) {
                    printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return;
                }
                memcpy(package, epoll->wspackage, epoll->wsuselen);
                sfree(epoll->wspackage);
                epoll->wspackage = package;
                epoll->wspackagecap = packagecap;
            }
            memcpy(epoll->wspackage + epoll->wsuselen, buff, len);
            epoll->wsuselen += len;
            if (epoll->wsuselen < epoll->wspackagelen) {
                return;
            }
            memcpy(buff, epoll->wspackage, epoll->wsuselen);
            len = epoll->wsuselen;
            sfree(epoll->wspackage);
            epoll->wspackagecap = 0;
            epoll->wspackagelen = 0;
            epoll->wsuselen = 0;
        }
        unsigned char *mask;
        unsigned char *data;
        unsigned int packagelen;
LOOP:
        // printf("in %s, at %d\n", __FILE__, __LINE__);
        if (len < 2) {
            printf("ws data so short, in %s, at %d\n", __FILE__, __LINE__);
            Epoll_Delete(epoll);
            return;
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
                    Epoll_Delete(epoll);
                    return;
                }
                mask = buff + 4;
                data = mask + 4;
                datalen = ((unsigned short)buff[2] << 8) | (unsigned short)buff[3];
                packagelen = datalen + 8;
            } else {
                if (len < 10) {
                    printf("ws data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return;
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
                    Epoll_Delete(epoll);
                    return;
                }
                data = buff + 4;
                datalen = ((unsigned short)buff[2] << 8) | (unsigned short)buff[3];
                packagelen = datalen + 4;
            } else {
                if (len < 10) {
                    printf("ws data so short, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return;
                }
                data = buff + 6;
                datalen = ((unsigned long)buff[2] << 56) | ((unsigned long)buff[3] << 48) | ((unsigned long)buff[4] << 40) | ((unsigned long)buff[5] << 32) | ((unsigned long)buff[6] << 24) | ((unsigned long)buff[7] << 16) | ((unsigned long)buff[8] << 8) | (unsigned long)buff[9];
                packagelen = datalen + 6;
            }
        }
        if (packagelen > len) { // 数据并未获取完毕，需要创建缓存并反复拉取数据
            unsigned char *package = (unsigned char*)smalloc(packagelen);
            if (package == NULL) {
                printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Delete(epoll);
                return;
            }
            memcpy(package, buff, len);
            epoll->wspackage = package;
            epoll->wspackagecap = packagelen;
            epoll->wspackagelen = packagelen;
            epoll->wsuselen = len;
            return;
        }
        if (mask) {
            for (unsigned int i = 0 ; i < datalen ; ++i) {
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
                    // printf("in %s, at %d\n", __FILE__, __LINE__);
                    HandleMqttClientRequest(epoll, data, datalen);
                    break;
                case 0x8:
                    // printf("in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);break; // 断开连接
                case 0x9: Epoll_Write(epoll, pong, sizeof(pong));break; // ping
            }
        } else {
            // 不是最后一个数据包，至今未出现过，可能要数据量超过4G才会出现。
            printf("0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x, in %s, at %d\n", buff[0], buff[1], buff[2], buff[3], buff[4], buff[5], __FILE__, __LINE__);
        }
        if (packagelen == len) {
            return;
        }
        len -= packagelen;
        memcpy(buff, buff + packagelen, len);
        goto LOOP;
    } else {
        char *method, *path, *version;
        struct HTTPPARAM httpparam[30];
        unsigned int size;
        int k, p, headlen;
        unsigned long packagelen;
        if (epoll->httphead != NULL) {
            if (epoll->wsuselen + len > epoll->wspackagecap) {
                unsigned int packagecap = epoll->wsuselen + len;
                unsigned char *package = (unsigned char*)smalloc(packagecap);
                if (package == NULL) {
                    printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Delete(epoll);
                    return;
                }
                memcpy(package, epoll->wspackage, epoll->wsuselen);
                sfree(epoll->wspackage);
                epoll->wspackage = package;
                epoll->wspackagecap = packagecap;
            }
            memcpy(epoll->wspackage + epoll->wspackagelen, buff, len);
            epoll->wsuselen += len;
            if (epoll->wsuselen < epoll->wspackagelen) {
                return;
            }
            packagelen = epoll->wspackagelen;
            memcpy(buff, epoll->wspackage, epoll->wsuselen);
            len = epoll->wsuselen;
            sfree(epoll->wspackage);
            epoll->wspackagecap = 0;
            epoll->wspackagelen = 0;
            epoll->wsuselen = 0;
            method = epoll->httphead->httpmethod;
            path = epoll->httphead->httppath;
            version = epoll->httphead->httpversion;
            memcpy(httpparam, epoll->httphead->httpparam, epoll->httphead->httpparam_size);
            size = epoll->httphead->httpparam_size;
            p = epoll->httphead->p;
            k = epoll->httphead->k;
            headlen = epoll->httphead->headlen;
            sfree(epoll->httphead);
            epoll->httphead = NULL;
        } else {
            size = 30;
            k = -1;
            p = -1;
            headlen = ParseHttpHeader(buff, len, &method, &path, &version, httpparam, &size);
            if (headlen < 0) {
                printf("Parse Http Header fail, in %s, at %d\n", __FILE__, __LINE__);
                Epoll_Write(epoll, ERRORPAGE, sizeof(ERRORPAGE));
                Epoll_Delete(epoll);
                return;
            }
            packagelen = len;
            int l = -1;
            for (int i = 0 ; i < size ; ++i) {
                if (!strcmp(httpparam[i].key, "Sec-WebSocket-Key")) {
                    k = i;
                }
                if (!strcmp(httpparam[i].key, "Sec-WebSocket-Protocol")) {
                    p = i;
                }
                if (!strcmp(httpparam[i].key, "Content-Length")) {
                    packagelen = atoi(httpparam[i].value) + headlen;
                    l = i;
                }
                if (k != -1 && p != -1 && l != -1) {
                    break;
                }
            }
            if (packagelen > len) {
                unsigned char *wspackage = (unsigned char*)smalloc(packagelen);
                if (wspackage == NULL) {
                    printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Write(epoll, PAGE500, sizeof(PAGE500));
                    Epoll_Delete(epoll);
                    return;
                }
                memcpy(wspackage, buff, len);
                epoll->wspackage = wspackage;
                epoll->wspackagelen = packagelen;
                epoll->wspackagecap = packagelen;
                epoll->wsuselen = len;
                struct HTTPHEAD *httphead = (struct HTTPHEAD*)smalloc(sizeof(struct HTTPHEAD));
                if (httphead == NULL) {
                    printf("malloc fail, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Write(epoll, PAGE500, sizeof(PAGE500));
                    Epoll_Delete(epoll);
                    return;
                }
                httphead->httpmethod = method;
                httphead->httppath = path;
                httphead->httpversion = version;
                memcpy(httphead->httpparam, httpparam, size);
                httphead->httpparam_size = size;
                httphead->p = p;
                httphead->k = k;
                httphead->headlen = headlen;
                epoll->httphead = httphead;
                return;
            }
        }
        if (!strcmp(method, "GET")) { // 是GET请求
            if (!strcmp(path, "/mqtt")) { // 是mqtt请求
                if (k == -1) {
                    printf("not found Sec-WebSocket-Key, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Write(epoll, ERRORPAGE, sizeof(ERRORPAGE));
                    Epoll_Delete(epoll);
                    return;
                }
                if (p == -1) {
                    printf("not found Sec-WebSocket-Protocol, in %s, at %d\n", __FILE__, __LINE__);
                    Epoll_Write(epoll, ERRORPAGE, sizeof(ERRORPAGE));
                    Epoll_Delete(epoll);
                    return;
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
                Epoll_Write(epoll, s, res_len);
                epoll->wsstate = 1;
            } else if (!strcmp(path, "/getclientsnum")) {
                // printf("in %s, at %d\n", __FILE__, __LINE__);
                unsigned int num = GetClientsNum();
                char body[16];
                unsigned char bodylen = sprintf(body, "%u", num);
                char http[128];
                unsigned char httplen = sprintf(http, HTTPOKHEAD, bodylen, body);
                Epoll_Write(epoll, http, httplen);
                Epoll_Delete(epoll);
            } else if (!memcmp(path, "/checkclientstatus/", 19)) {
                // printf("in %s, at %d\n", __FILE__, __LINE__);
                unsigned char res = CheckClientStatus(path+19, strlen(path+19));
                char body[16];
                unsigned char bodylen = sprintf(body, "%u", res);
                char http[128];
                unsigned char httplen = sprintf(http, HTTPOKHEAD, bodylen, body);
                Epoll_Write(epoll, http, httplen);
                Epoll_Delete(epoll);
            } else if (!strcmp(path, "/showclients")) {
                // printf("in %s, at %d\n", __FILE__, __LINE__);
                ShowClients();
                Epoll_Write(epoll, SUCCESSPAGE, sizeof(SUCCESSPAGE));
                Epoll_Delete(epoll);
            } else if (!strcmp(path, "/showtopics")) {
                // printf("in %s, at %d\n", __FILE__, __LINE__);
                ShowTopics();
                Epoll_Write(epoll, SUCCESSPAGE, sizeof(SUCCESSPAGE));
                Epoll_Delete(epoll);
            } else if (!strcmp(path, "/getmallocnum")) {
                // printf("in %s, at %d\n", __FILE__, __LINE__);
                int malloc_num = GetMallocNum();
                char body[16];
                unsigned char bodylen = sprintf(body, "%d", malloc_num);
                char http[128];
                unsigned char httplen = sprintf(http, HTTPOKHEAD, bodylen, body);
                Epoll_Write(epoll, http, httplen);
                Epoll_Delete(epoll);
            } else {
                Epoll_Write(epoll, ERRORPAGE, sizeof(ERRORPAGE));
                Epoll_Delete(epoll);
            }
        } else if (!strcmp(method, "POST")) {
            Epoll_Write(epoll, ERRORPAGE, sizeof(ERRORPAGE));
            Epoll_Delete(epoll);
        } else {
            Epoll_Write(epoll, ERRORPAGE, sizeof(ERRORPAGE));
            Epoll_Delete(epoll);
        }
    }
}

static void Ws_New_Connect (EPOLL *e, unsigned char *buff) {
    struct sockaddr_in sin;
    socklen_t in_addr_len = sizeof(struct sockaddr_in);
    int fd = accept(e->fd, (struct sockaddr*)&sin, &in_addr_len);
    if (fd < 0) {
        printf("accept a new fd fail, in %s, at %d\n", __FILE__, __LINE__);
        return;
    }
    EPOLL *epoll = add_fd_to_poll(fd, 1);
    if (epoll == NULL) {
        close(fd);
        return;
    }
    epoll->read = Ws_Read_Handler;
    epoll->write = Ws_Write_Connect;
    if (e->tls) {
        struct ConfigData *configdata = GetConfig();
        SSL *tls = SSL_new(configdata->ctx);
        SSL_set_fd(tls, fd);
        epoll->tls = tls;
        SSL_set_accept_state(tls);
    }
}

int Ws_Create () {
    struct ConfigData *configdata = GetConfig ();
    if (configdata->wsport) {
        int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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
        EPOLL *epoll = add_fd_to_poll(fd, 0);
        if (epoll == NULL) {
            printf("add fd to poll fail, fd: %d, in %s, at %d\n", fd, __FILE__, __LINE__);
            close(fd);
            return -4;
        }
        epoll->read = Ws_New_Connect;
    }
    if (configdata->wssport) {
        int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd < 0) {
            printf("create socket fail, in %s, at %d\n", __FILE__, __LINE__);
            return -5;
        }
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(struct sockaddr_in));
        sin.sin_family = AF_INET; // ipv4
        sin.sin_addr.s_addr = INADDR_ANY; // 任意ip
        sin.sin_port = htons(configdata->wssport);
        if (bind(fd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
            printf("port %d bind fail, in %s, at %d\n", configdata->wssport, __FILE__, __LINE__);
            close(fd);
            return -6;
        }
        if (listen(fd, 16) < 0) {
            printf("listen port %d fail, in %s, at %d\n", configdata->wssport, __FILE__, __LINE__);
            close(fd);
            return -7;
        }
        EPOLL *epoll = add_fd_to_poll(fd, 0);
        if (epoll == NULL) {
            printf("add fd to poll fail, fd: %d, in %s, at %d\n", fd, __FILE__, __LINE__);
            close(fd);
            return -8;
        }
        epoll->read = Ws_New_Connect;
        epoll->tls = (SSL*)1;
        epoll->tlsok = 1;
    }
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
    for (int i = 0 ; i < str_size ; ++i) {
        switch (step) {
            case 0: // 寻找mothod开始
                if (str[i] != ' ') {
                    *method = str + i;
                    ++step;
                }
                break;
            case 1: // 寻找mothod的结束，path的开始
                if (str[i] == ' ') {
                    str[i] = '\0';
                    ++step;
                }
                break;
            case 2: // 寻找path开始
                if (str[i] != ' ') {
                    *path = str + i;
                    ++step;
                }
                break;
            case 3: // 寻找path结束
                if (str[i] == ' ') {
                    str[i] = '\0';
                    ++step;
                }
                break;
            case 4: // 寻找version开始
                if (str[i] != ' ') {
                    *version = str + i;
                    ++step;
                }
                break;
            case 5: // 寻找version结束
                if (str[i-1] == '\r' && str[i] == '\n') {
                    str[i-1] = '\0';
                    step = 7;
                } else if (str[i] == ' ') {
                    str[i] = '\0';
                    ++step;
                }
                break;
            case 6: // 寻找换行
                if (str[i-1] == '\r' && str[i] == '\n') {
                    step = 7;
                }
            case 7: // 寻找PARAMKEY开始
                if ((str[i-3] == '\0' || str[i-3] == '\r') && str[i-2] == '\n' && str[i-1] == '\r' && str[i] == '\n') { // 正常退出处
                    *httpparam_size = httpParamNum;
                    return i;
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
                    ++step;
                }
                break;
            case 9: // 寻找PARAMVALUE开始
                if (str[i] != ' ' && str[i] != ':') {
                    httpparam[httpParamNum].value = str + i;
                    ++step;
                }
                break;
            case 10: // 寻找PARAMKEY结束
                if (str[i-1] == '\r' && str[i] == '\n') {
                    str[i-1] = '\0';
                    ++httpParamNum;
                    step = 7;
                } else if (str[i] == ' ') {
                    str[i] = '\0';
                    ++httpParamNum;
                    ++step;
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
