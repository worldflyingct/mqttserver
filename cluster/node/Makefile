CC=gcc
CFLAGS=-std=gnu99 -O3 -static

node: main.o mqtt.o ws.o tcp.o event_poll.o config.o cjson.o base64.o sha1.o sha256.o
	$(CC) $(CFLAGS) -o $@ $^

main.o: main.c event_poll.h ws.h config.h
	$(CC) $(CFLAGS) -c -o $@ main.c

mqtt.o: mqtt.c mqtt.h event_poll.h sha256.h
	$(CC) $(CFLAGS) -c -o $@ mqtt.c

ws.o: ws.c event_poll.h mqtt.h config.h sha1.h base64.h
	$(CC) $(CFLAGS) -c -o $@ ws.c

tcp.o: tcp.c event_poll.h mqtt.h config.h
	$(CC) $(CFLAGS) -c -o $@ tcp.c

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

sha256.o: sha256.c
	$(CC) $(CFLAGS) -c -o $@ sha256.c

clean:
	rm *.o
