#ifndef __MQTT_H__
#define __MQTT_H__

struct SubScribeList {
    struct TopicList *topiclist;
    struct SubScribeList *head;
    struct SubScribeList *tail;
};

int HandleMqttClientRequest (EPOLL *epoll, unsigned char *buff, unsigned long len);
void PublishData (unsigned char *topic, unsigned long topiclen, unsigned char *msg, unsigned long msglen, unsigned char *buff);
void ShowClients ();
int DeleteMqttClient (EPOLL *epoll, unsigned char *buff);
int GetMqttLength (unsigned char *buff, unsigned long len, unsigned long *packagelen, unsigned long *offset);

#endif