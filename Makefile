CC = gcc
CCFLAGS = -Wall -lm

.PHONY: build clean

build: server subscriber

server: server.c treap.c list.c queue.c sll.c
	$(CC) -o $@ $^ $(CCFLAGS)

subscriber: subscriber.c list.c queue.c
	$(CC) -o $@ $^ $(CCFLAGS)

clean:
	rm -f server subscriber