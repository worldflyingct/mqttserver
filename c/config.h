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
    unsigned char mqttkey[33];
    SSL_CTX *ctx;
};

struct ConfigData* InitConfig ();
struct ConfigData* GetConfig ();

#endif
