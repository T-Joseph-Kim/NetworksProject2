// client.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>       // Provides access to POSIX operating system API
#include <arpa/inet.h>    // Definitions for internet operations
#include <dirent.h>       // Directory entry operations
#include <sys/stat.h>     // Data returned by the `stat` function
#include <openssl/md5.h>  // OpenSSL library for MD5 hashing
#include <stdint.h>       // For fixed-size integer types
#include "messages.h"     // Header file with message structs

#define PORT 8080
#define MAX_CACHE_FILES 100  // Define a reasonable size for the cache

// Function prototypes
void list_files(int sock);
void diff_files(int sock);
void pull_files(int sock);
void compute_file_md5(const char *filename, char *md5_str);

// Cache for storing missing filenames
char missing_files_cache[MAX_CACHE_FILES][FILENAME_SIZE];
int cache_count = 0;

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char command[BUFFER_SIZE];

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }

    // Define server address
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 address from text to binary
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return -1;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }

    printf("Connected to server.\n");

    // Command loop
    while (1) {
        printf("\nEnter command (LIST, DIFF, PULL, LEAVE): ");
        fgets(command, BUFFER_SIZE, stdin);
        command[strcspn(command, "\n")] = 0; // Remove newline

        MessageHeader header;
        memset(&header, 0, sizeof(header));

        if (strcasecmp(command, "LIST") == 0) {
            header.type = htonl(MSG_LIST);
            header.length = htonl(0);
            send(sock, &header, sizeof(header), 0);
            list_files(sock);
        } else if (strcasecmp(command, "DIFF") == 0) {
            // Clear cache before performing DIFF
            cache_count = 0;
            diff_files(sock);
        } else if (strcasecmp(command, "PULL") == 0) {
            pull_files(sock);
        } else if (strcasecmp(command, "LEAVE") == 0) {
            header.type = htonl(MSG_LEAVE);
            header.length = htonl(0);
            send(sock, &header, sizeof(header), 0);
            break;
        } else {
            printf("Invalid command.\n");
        }
    }

    close(sock);
    return 0;
}

// Function to list files from the server
void list_files(int sock) {
    MessageHeader header;
    header.type = htonl(MSG_LIST);
    header.length = htonl(0); // No additional data needed for LIST

    // Send the LIST command to the server
    if (send(sock, &header, sizeof(header), 0) < 0) {
        perror("Failed to send LIST command");
        return;
    }

    // Receive and print file names from the server
    ResponseMessage response_msg;
    while (recv(sock, &response_msg, sizeof(response_msg), 0) > 0) {
        if (ntohl(response_msg.header.type) != MSG_RESPONSE) {
            break; // Exit if it's not a response message
        }

        printf("File: %s\n", response_msg.response); // Print the received file name
    }
}

// Modify the DIFF function to store missing filenames in cache
void diff_files(int sock) {
    DIR *dir;
    struct dirent *entry;
    char filepath[FILENAME_SIZE];
    char md5_str[MD5_HASH_SIZE];

    if ((dir = opendir(".")) == NULL) {
        perror("Unable to open directory");
        return;
    }

    // Send MD5 hashes of all files
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) { // Regular file
            snprintf(filepath, sizeof(filepath), "./%s", entry->d_name);
            compute_file_md5(filepath, md5_str);

            // Send filename and MD5 hash to server
            FilenameMessage filename_msg;
            memset(&filename_msg, 0, sizeof(filename_msg));
            filename_msg.header.type = htonl(MSG_FILENAME);
            filename_msg.header.length = htonl(strlen(entry->d_name));
            strncpy(filename_msg.filename, entry->d_name, FILENAME_SIZE);

            send(sock, &filename_msg, sizeof(filename_msg), 0);

            // Send MD5 hash
            MD5Message md5_msg;
            memset(&md5_msg, 0, sizeof(md5_msg));
            md5_msg.header.type = htonl(MSG_MD5);
            md5_msg.header.length = htonl(strlen(md5_str));
            strncpy(md5_msg.md5_hash, md5_str, MD5_HASH_SIZE);

            send(sock, &md5_msg, sizeof(md5_msg), 0);
        }
    }

    closedir(dir);

    // Indicate to the server that all hashes have been sent
    MessageHeader done_msg;
    done_msg.type = htonl(MSG_DONE);  // New message type to indicate completion
    done_msg.length = htonl(0);
    send(sock, &done_msg, sizeof(done_msg), 0);

    // Wait for server response about missing files and store in cache
    ResponseMessage response_msg;
    while (recv(sock, &response_msg, sizeof(response_msg), 0) > 0) {
        if (ntohl(response_msg.header.type) == MSG_RESPONSE) {
            if (cache_count < MAX_CACHE_FILES) {
                strncpy(missing_files_cache[cache_count], response_msg.response, FILENAME_SIZE);
                cache_count++;
            } else {
                printf("Cache full, unable to store more missing files.\n");
                break;
            }
        }
    }
}

// Modify the PULL function to request all missing files from the cache
void pull_files(int sock) {
    for (int i = 0; i < cache_count; i++) {
        const char *filename = missing_files_cache[i];
        
        // Send PULL command header
        MessageHeader header;
        header.type = htonl(MSG_PULL);
        header.length = htonl(strlen(filename) + 1); // Include null terminator
        send(sock, &header, sizeof(header), 0);
        
        // Send filename
        send(sock, filename, strlen(filename) + 1, 0);
        
        // Receive and save the pulled file
        FILE *fp = fopen(filename, "wb");
        if (fp == NULL) {
            printf("Failed to create file '%s'.\n", filename);
            continue;
        }

        FileDataMessage file_data;
        int bytes_received;
        while (1) {
            // Receive header
            bytes_received = recv(sock, &file_data.header, sizeof(file_data.header), 0);
            if (bytes_received <= 0) break;
            
            file_data.header.type = ntohl(file_data.header.type);
            file_data.header.length = ntohl(file_data.header.length);
            
            if (file_data.header.type != MSG_FILE_DATA) break;
            
            // Receive data
            bytes_received = recv(sock, file_data.data, file_data.header.length, 0);
            if (bytes_received <= 0) break;
            
            fwrite(file_data.data, 1, bytes_received, fp);
        }

        fclose(fp);
        printf("File '%s' pulled successfully.\n", filename);
    }

    // Clear cache after pulling files
    cache_count = 0;
}

// Function to compute MD5 hash of a file
void compute_file_md5(const char *filename, char *md5_str) {
    unsigned char c[MD5_DIGEST_LENGTH];
    int i;
    FILE *inFile = fopen(filename, "rb");
    MD5_CTX mdContext;
    int bytes;
    unsigned char data[BUFFER_SIZE];

    if (inFile == NULL) {
        strcpy(md5_str, "");
        return;
    }

    MD5_Init(&mdContext);
    while ((bytes = fread(data, 1, BUFFER_SIZE, inFile)) != 0)
        MD5_Update(&mdContext, data, bytes);
    MD5_Final(c, &mdContext);

    for (i = 0; i < MD5_DIGEST_LENGTH; i++)
        sprintf(&md5_str[i * 2], "%02x", c[i]);

    md5_str[32] = '\0'; // Null-terminate the string
    fclose(inFile);
}
