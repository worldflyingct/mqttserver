#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include "event_poll.h"
#include "mqtt.h"

static int pipefd[2];

static void Timer_Read_Handler (EPOLL *epoll, unsigned char *buff) { // 作为mqtt处理
    read(epoll->fd, buff, 1024);
    CheckMqttClients();
}

static void signalHandler(int signo) {
    if (signo == SIGALRM) {
        write(pipefd[1], "0", 1);
    }
}

int Init_Timer() {
    if (pipe(pipefd)) {
        return -1;
    }
    EPOLL *epoll = add_fd_to_poll(pipefd[0], 0);
    if (epoll == NULL) {
        printf("add fd to poll fail, fd: %d, in %s, at %d\n", pipefd[0], __FILE__, __LINE__);
        close(pipefd[0]);
        close(pipefd[1]);
        return -2;
    }
    epoll->read = Timer_Read_Handler;
    signal(SIGALRM, signalHandler);
    struct itimerval timer, oldtimer;
    timer.it_value.tv_sec = 1;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 1;
    timer.it_interval.tv_usec = 0;
    if (setitimer(ITIMER_REAL, &timer, &oldtimer)) {
        return -3;
    }
}
