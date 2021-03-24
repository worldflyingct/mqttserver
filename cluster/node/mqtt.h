#ifndef __MQTT_H__
#define __MQTT_H__

#include "event_poll.h"

int HandleMqttClientRequest (EPOLL *epoll, unsigned char *buff, unsigned long len);

#endif
