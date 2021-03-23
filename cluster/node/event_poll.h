#ifndef __EVENT_POLL_H___
#define __EVENT_POLL_H___

#include <sys/epoll.h>

typedef int EVENT_FUNCTION (int event, void *ptr);
typedef int WRITE_FUNCTION (void *ptr, unsigned char *data, unsigned int len);
typedef int DELETE_FUNCTION (void *ptr);

int event_poll_create ();
void event_poll_loop ();
int add_fd_to_poll (void *ptr);
void remove_fd_from_poll (void *ptr);

#endif
