# Makefile for UFmyMusic client and server on Windows

# Compiler and flags
CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lws2_32 -ladvapi32

# Targets
all: server.exe client.exe

server.exe: server.c
	$(CC) $(CFLAGS) -o server.exe server.c $(LDFLAGS)

client.exe: client.c
	$(CC) $(CFLAGS) -o client.exe client.c $(LDFLAGS)

clean:
	del server.exe client.exe

.PHONY: all clean
