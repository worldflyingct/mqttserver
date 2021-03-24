#ifndef __CONFIG_H__
#define __CONFIG_H__

struct ConfigData {
    unsigned short tcpport;
    unsigned short wsport;
    unsigned char mqttuser[17];
    unsigned char mqttkey[17];
};

struct ConfigData* InitConfig ();
struct ConfigData* GetConfig ();

#endif
