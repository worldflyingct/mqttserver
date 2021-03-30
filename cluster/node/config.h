#ifndef __CONFIG_H__
#define __CONFIG_H__

struct ConfigData {
    unsigned short tcpport;
    unsigned short wsport;
    unsigned char mqttuser[33];
    unsigned char mqttkey[33];
    unsigned char serverdomain[65];
    unsigned short serverport;
    unsigned char regok;
};

struct ConfigData* InitConfig ();
struct ConfigData* GetConfig ();

#endif
