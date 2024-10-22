#ifndef MESSAGES_H
#define MESSAGES_H

#include <stdint.h>

#define BUFFER_SIZE 4096
#define FILENAME_SIZE 256
#define MD5_HASH_SIZE 33

#pragma pack(push, 1)

typedef enum {
    MSG_LIST = 1,
    MSG_DIFF,
    MSG_PULL,
    MSG_LEAVE,
    MSG_FILENAME,
    MSG_MD5,
    MSG_FILE_DATA,
    MSG_RESPONSE,
    MSG_DONE,
    CLIENT_DONE,
    SERVER_DONE,
    NO_FILE_FOUND,
} MessageType;


typedef struct {
    uint32_t type;
    uint32_t length;
} MessageHeader;

typedef struct {
    MessageHeader header;
    char filename[FILENAME_SIZE];
} FilenameMessage;

typedef struct {
    MessageHeader header;
    char md5_hash[MD5_HASH_SIZE];
} MD5Message;

typedef struct {
    MessageHeader header;
    uint8_t data[BUFFER_SIZE];
} FileDataMessage;

typedef struct {
    MessageHeader header;
    char response[BUFFER_SIZE];
} ResponseMessage;

#pragma pack(pop)

#endif
