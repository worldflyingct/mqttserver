#ifndef __MQTT_H__
#define __MQTT_H__

struct SubScribeList {
    struct TopicList *topiclist;
    struct SubScribeList *head;
    struct SubScribeList *tail;
};

void HandleMqttClientRequest (EPOLL *epoll, uint8_t *buff, uint64_t len);
void DeleteMqttClient (EPOLL *epoll);
void ShowClients ();
void ShowTopics ();
uint8_t CheckClientStatus (char *clientid, uint32_t clientidlen);
uint32_t GetClientsNum ();
void CheckMqttClients ();

#endif
