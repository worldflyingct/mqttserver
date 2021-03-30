#ifndef __MQTT_H__
#define __MQTT_H__

typedef struct EPOLL EPOLL;

struct SubScribeList {
    struct TopicList *topiclist;
    struct SubScribeList *head;
    struct SubScribeList *tail;
};

int HandleMqttClientRequest (EPOLL *epoll, unsigned char *buff, unsigned long len);

#endif
