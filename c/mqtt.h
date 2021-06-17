#ifndef __MQTT_H__
#define __MQTT_H__

struct SubScribeList {
    struct TopicList *topiclist;
    struct SubScribeList *head;
    struct SubScribeList *tail;
};

void HandleMqttClientRequest (EPOLL *epoll, unsigned char *buff, unsigned long len);
void DeleteMqttClient (EPOLL *epoll);
void ShowClients ();
void ShowTopics ();
unsigned char CheckClientStatus (char *clientid, unsigned int clientidlen);
unsigned int GetClientsNum ();
void CheckMqttClients ();

#endif
