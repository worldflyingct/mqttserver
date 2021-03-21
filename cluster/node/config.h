#ifndef __CONFIG_H__
#define __CONFIG_H__

struct ConfigData {
    unsigned short tcpport;
    unsigned short wsport;
};

struct ConfigData* InitConfig ();
struct ConfigData* GetConfig ();

#endif
