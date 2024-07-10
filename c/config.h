#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <openssl/ssl.h>

struct ConfigData {
    uint16_t tcpport;
    uint16_t tlsport;
    uint16_t wsport;
    uint16_t wssport;
    uint8_t crtpath[129];
    uint8_t keypath[129];
    uint8_t mqttuser[33];
    uint8_t mqttuserlen;
    uint8_t mqttkey[33];
    uint8_t mqttkeylen;
    uint8_t mqttkeymode; // 0为一般模式，1为根据当前时间戳与sha256计算模式
    SSL_CTX *ctx;
    uint8_t tcpkeepidle;
    uint8_t tcpkeepinterval;
    uint8_t tcpkeepcount;
};

struct ConfigData* InitConfig ();
struct ConfigData* GetConfig ();

#endif
