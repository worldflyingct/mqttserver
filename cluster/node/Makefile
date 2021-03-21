CC=gcc
CFLAGS=-std=gnu99 -O3 -static

node: main.o ws.o event_poll.o config.o cjson.o base64.o sha1.o
	$(CC) $(CFLAGS) -o $@ $^

main.o: main.c event_poll.h ws.h config.h
	$(CC) $(CFLAGS) -c -o $@ main.c

ws.o: ws.c sha1.h base64.h config.h
	$(CC) $(CFLAGS) -c -o $@ ws.c

event_poll.o: event_poll.c event_poll.h
	$(CC) $(CFLAGS) -c -o $@ event_poll.c

config.o: config.c config.h cJSON.h
	$(CC) $(CFLAGS) -c -o $@ config.c

cjson.o: cJSON.c
	$(CC) $(CFLAGS) -c -o $@ cJSON.c

base64.o: base64.c
	$(CC) $(CFLAGS) -c -o $@ base64.c

sha1.o: sha1.c
	$(CC) $(CFLAGS) -c -o $@ sha1.c
