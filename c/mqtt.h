#ifndef __MQTT_H__
#define __MQTT_H__

struct SubScribeList {
    struct TopicList *topiclist;
    struct SubScribeList *head;
    struct SubScribeList *tail;
};

int HandleMqttClientRequest (EPOLL *epoll, unsigned char *buff, unsigned long len);
void PublishData (unsigned char *topic, unsigned short topiclen, unsigned char *msg, unsigned int msglen, unsigned char *buff);
void ShowClients ();
void ShowTopics ();
unsigned char CheckClientStatus (char *clientid, unsigned int clientidlen);
int DeleteMqttClient (EPOLL *epoll, unsigned char *buff);
unsigned int GetClientsNum ();

#endif
