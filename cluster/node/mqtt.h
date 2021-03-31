#ifndef __MQTT_H__
#define __MQTT_H__

struct SubScribeList {
    struct TopicList *topiclist;
    struct SubScribeList *head;
    struct SubScribeList *tail;
};

int HandleMqttClientRequest (EPOLL *epoll, unsigned char *buff, unsigned long len);
int PublishData (unsigned char *buff, unsigned long len, unsigned long offset);
void ShowClients ();
int DeleteMqttClient (EPOLL *epoll);
int GetMqttLength (unsigned char *buff, unsigned long len, unsigned long *packagelen, unsigned long *offset);

#endif
