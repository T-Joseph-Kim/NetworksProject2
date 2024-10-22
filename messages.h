#ifndef MESSAGES_H
#define MESSAGES_H

#include <stdint.h>

#define BUFFER_SIZE 4096
#define FILENAME_SIZE 256
#define MD5_HASH_SIZE 33 // 32 chars + null terminator

// Ensure the struct is packed to avoid padding
#pragma pack(push, 1)

// Enumeration for message types
typedef enum {
    MSG_LIST = 1,
    MSG_DIFF,
    MSG_PULL,
    MSG_LEAVE,
    MSG_FILENAME,
    MSG_MD5,
    MSG_FILE_DATA,
    MSG_RESPONSE,
    MSG_DONE,  // New message type to indicate the end of MD5 transfer
    CLIENT_DONE,
    SERVER_DONE
} MessageType;


// General message header
typedef struct {
    uint32_t type;   // MessageType
    uint32_t length; // Length of the data following the header
} MessageHeader;

// Filename message
typedef struct {
    MessageHeader header;
    char filename[FILENAME_SIZE];
} FilenameMessage;

// MD5 hash message
typedef struct {
    MessageHeader header;
    char md5_hash[MD5_HASH_SIZE];
} MD5Message;

// File data message
typedef struct {
    MessageHeader header;
    uint8_t data[BUFFER_SIZE];
} FileDataMessage;

// Response message
typedef struct {
    MessageHeader header;
    char response[BUFFER_SIZE];
} ResponseMessage;

#pragma pack(pop)

#endif // MESSAGES_H
