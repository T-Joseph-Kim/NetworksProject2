# Makefile for UFmyMusic client and server

CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lpthread -lssl -lcrypto

# Targets
all: server client

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) -o client client.c $(LDFLAGS)

clean:
	rm -f server client

.PHONY: all clean