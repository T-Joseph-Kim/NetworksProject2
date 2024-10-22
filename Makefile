CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lpthread -lssl -lcrypto

# Directories for source and target
SERVER_DIR = server
CLIENT_DIR = client
INCLUDE_DIR = .

# Executable names
SERVER_EXEC = $(SERVER_DIR)/server
CLIENT_EXEC = $(CLIENT_DIR)/client

# Object files
SERVER_OBJS = $(SERVER_DIR)/server.c
CLIENT_OBJS = $(CLIENT_DIR)/client.c

# Targets
all: $(SERVER_EXEC) $(CLIENT_EXEC)

# Compile the server executable
$(SERVER_EXEC): $(SERVER_OBJS) messages.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -o $@ $(SERVER_OBJS) $(LDFLAGS)

# Compile the client executable
$(CLIENT_EXEC): $(CLIENT_OBJS) messages.h
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -o $@ $(CLIENT_OBJS) $(LDFLAGS)

# Clean command
clean:
	rm -f $(SERVER_EXEC) $(CLIENT_EXEC)

.PHONY: all clean
