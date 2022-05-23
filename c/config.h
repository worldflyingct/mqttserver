#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <openssl/ssl.h>

struct ConfigData {
    unsigned short tcpport;
    unsigned short tlsport;
    unsigned short wsport;
    unsigned short wssport;
    unsigned char crtpath[129];
    unsigned char keypath[129];
    unsigned char mqttuser[33];
    unsigned char mqttuserlen;
    unsigned char mqttkey[33];
    unsigned char mqttkeylen;
    unsigned char mqttkeymode; // 0为一般模式，1为根据当前时间戳与sha256计算模式
    SSL_CTX *ctx;
    unsigned char tcpkeepidle;
    unsigned char tcpkeepinterval;
    unsigned char tcpkeepcount;
};

struct ConfigData* InitConfig ();
struct ConfigData* GetConfig ();

#endif
