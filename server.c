// server.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>       // Provides access to POSIX operating system API
#include <arpa/inet.h>    // Definitions for internet operations
#include <dirent.h>       // Directory entry operations
#include <sys/stat.h>     // Data returned by the `stat` function
#include <openssl/md5.h>  // OpenSSL library for MD5 hashing
#include "messages.h"     // Header file with message structs

#define PORT 8080

// Function prototypes
void handle_list(int client_socket);
void handle_diff(int client_socket);
void handle_pull(int client_socket);
void send_file(int client_socket, const char *filename);
void compute_file_md5(const char *filename, char *md5_str);

int main() {
    int server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind the socket to the port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Start listening for connections
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);

    // Accept incoming connections
    while ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) >= 0) {
        printf("Accepted connection from client\n");

        while (1) {
            MessageHeader header;
            int valread = recv(client_socket, &header, sizeof(header), 0);
            if (valread <= 0) break;

            header.type = ntohl(header.type);
            header.length = ntohl(header.length);

            if (header.type == MSG_LIST) {
                handle_list(client_socket);
            } else if (header.type == MSG_DIFF) {
                handle_diff(client_socket);
            } else if (header.type == MSG_PULL) {
                handle_pull(client_socket);
            } else if (header.type == MSG_LEAVE) {
                printf("Client disconnected\n");
                break;
            }
        }

        close(client_socket);
    }

    close(server_fd);
    return 0;
}

// Handle LIST command: List all files in the server directory
void handle_list(int client_socket) {
    DIR *dir;
    struct dirent *entry;

    if ((dir = opendir(".")) == NULL) {
        perror("Unable to open directory");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) { // Regular file
            ResponseMessage response_msg;
            response_msg.header.type = htonl(MSG_RESPONSE);
            strncpy(response_msg.response, entry->d_name, FILENAME_SIZE);

            send(client_socket, &response_msg, sizeof(response_msg), 0);
        }
    }

    closedir(dir);
}

// Handle DIFF command: Compare client MD5 hashes with server files (ignore filenames)
void handle_diff(int client_socket) {
    char client_md5s[100][MD5_HASH_SIZE];  // Store client MD5s
    int client_file_count = 0;

    // Receive all MD5 hashes from client
    while (1) {
        MessageHeader header;
        if (recv(client_socket, &header, sizeof(header), 0) <= 0) break;

        header.type = ntohl(header.type);
        header.length = ntohl(header.length);

        if (header.type == MSG_MD5) {
            MD5Message md5_msg;
            recv(client_socket, &md5_msg, sizeof(md5_msg), 0);
            strcpy(client_md5s[client_file_count], md5_msg.md5_hash);
            client_file_count++;
        } else if (header.type == MSG_DONE) {
            // Client is done sending hashes, break loop
            break;
        }
    }

    // Compare client MD5s with server files
    DIR *dir;
    struct dirent *entry;
    char filepath[FILENAME_SIZE];
    char server_md5[MD5_HASH_SIZE];

    if ((dir = opendir(".")) == NULL) {
        perror("Unable to open directory");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {  // Regular file
            snprintf(filepath, sizeof(filepath), "./%s", entry->d_name);
            compute_file_md5(filepath, server_md5);

            // Compare server file MD5 with all client MD5 hashes
            int file_found = 0;
            for (int i = 0; i < client_file_count; i++) {
                if (strcmp(server_md5, client_md5s[i]) == 0) {
                    file_found = 1;
                    break;
                }
            }

            // If no matching MD5 hash found, send filename to client
            if (!file_found) {
                ResponseMessage response_msg;
                memset(&response_msg, 0, sizeof(response_msg));
                response_msg.header.type = htonl(MSG_RESPONSE);
                strncpy(response_msg.response, entry->d_name, FILENAME_SIZE);
                send(client_socket, &response_msg, sizeof(response_msg), 0);
            }
        }
    }

    closedir(dir);
}


// Handle PULL command: Send requested file to client
void handle_pull(int client_socket) {
    // Receive the filename from the client
    char filename[FILENAME_SIZE];
    recv(client_socket, filename, FILENAME_SIZE, 0);

    // Send the file to the client
    send_file(client_socket, filename);
}

// Send a file to the client
void send_file(int client_socket, const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("File not found");
        return;
    }

    FileDataMessage file_data;
    int bytes_read;

    // Send file data in chunks
    while ((bytes_read = fread(file_data.data, 1, BUFFER_SIZE, fp)) > 0) {
        file_data.header.type = htonl(MSG_FILE_DATA);
        file_data.header.length = htonl(bytes_read);
        send(client_socket, &file_data, sizeof(file_data.header) + bytes_read, 0);
    }

    fclose(fp);
}

// Compute the MD5 hash of a file
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
