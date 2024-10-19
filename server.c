// server.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>       // Provides access to POSIX operating system API
#include <pthread.h>      // POSIX threads library
#include <arpa/inet.h>    // Definitions for internet operations
#include <dirent.h>       // Directory entry operations
#include <sys/stat.h>     // Data returned by the `stat` function
#include <openssl/md5.h>  // OpenSSL library for MD5 hashing
#include <stdint.h>       // For fixed-size integer types
#include "messages.h"     // Header file with message structs

#define PORT 8080
#define MAX_CLIENTS 10

// Function prototypes
void *handle_client(void *client_socket);
void send_file_list(int client_socket);
void handle_diff(int client_socket);
void handle_pull(int client_socket);
void compute_file_md5(const char *filename, char *md5_str);

int main() {
    int server_fd, new_socket, *client_sock;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    pthread_t thread_id;

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Bind to the specified PORT
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    memset(address.sin_zero, '\0', sizeof address.sin_zero);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    printf("Server started on port %d\n", PORT);

    // Start listening for connections
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen");
        exit(EXIT_FAILURE);
    }

    printf("Waiting for connections...\n");

    while ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) >= 0) {
        printf("Connection accepted: socket %d\n", new_socket);

        client_sock = malloc(1);
        *client_sock = new_socket;

        // Create a new thread for each client
        if (pthread_create(&thread_id, NULL, handle_client, (void *)client_sock) < 0) {
            perror("Could not create thread");
            free(client_sock);
            continue;
        }

        printf("Handler assigned for socket %d\n", new_socket);
    }

    if (new_socket < 0) {
        perror("Accept");
        exit(EXIT_FAILURE);
    }

    return 0;
}

// Thread function to handle each client
void *handle_client(void *client_socket) {
    int sock = *(int *)client_socket;
    MessageHeader header;

    printf("Client connected: socket %d\n", sock);

    while (1) {
        int bytes_read = recv(sock, &header, sizeof(header), 0);
        if (bytes_read <= 0) {
            printf("Client disconnected: socket %d\n", sock);
            close(sock);
            break;
        }

        header.type = ntohl(header.type);
        header.length = ntohl(header.length);

        switch (header.type) {
            case MSG_LIST:
                send_file_list(sock);
                break;
            case MSG_DIFF:
                handle_diff(sock);
                break;
            case MSG_PULL:
                handle_pull(sock);
                break;
            case MSG_LEAVE:
                printf("Client requested to leave: socket %d\n", sock);
                close(sock);
                break;
            default:
                printf("Invalid command from client: socket %d\n", sock);
                // Optionally send an error response
                break;
        }
    }

    free(client_socket);
    pthread_exit(NULL);
    return NULL;
}

// Function to send the list of files in the server's directory
void send_file_list(int client_socket) {
    DIR *d;
    struct dirent *dir;
    char file_list[BUFFER_SIZE] = "";
    MessageHeader header;

    d = opendir(".");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            // Skip directories
            if (dir->d_type == DT_REG) {
                strcat(file_list, dir->d_name);
                strcat(file_list, "\n");
            }
        }
        closedir(d);
    }

    // Send response message
    header.type = htonl(MSG_RESPONSE);
    header.length = htonl(strlen(file_list) + 1);
    send(client_socket, &header, sizeof(header), 0);
    send(client_socket, file_list, strlen(file_list) + 1, 0);
}

// Function to handle the DIFF command
void handle_diff(int client_socket) {
    char filename[FILENAME_SIZE];
    char client_md5[MD5_HASH_SIZE];
    char server_md5[MD5_HASH_SIZE];
    MessageHeader header;
    ResponseMessage response;

    while (1) {
        // Receive filename message
        int bytes_read = recv(client_socket, &header, sizeof(header), 0);
        if (bytes_read <= 0) {
            break;
        }
        header.type = ntohl(header.type);
        header.length = ntohl(header.length);

        if (header.type != MSG_FILENAME) {
            break;
        }
        recv(client_socket, filename, header.length, 0);

        // Receive MD5 hash message
        bytes_read = recv(client_socket, &header, sizeof(header), 0);
        if (bytes_read <= 0) {
            break;
        }
        header.type = ntohl(header.type);
        header.length = ntohl(header.length);

        if (header.type != MSG_MD5) {
            break;
        }
        recv(client_socket, client_md5, header.length, 0);

        // Compute MD5 of the server's file
        compute_file_md5(filename, server_md5);

        // Compare MD5 hashes
        response.header.type = htonl(MSG_RESPONSE);
        if (strcmp(client_md5, server_md5) != 0) {
            strcpy(response.response, "DIFFERENT");
        } else {
            strcpy(response.response, "SAME");
        }
        response.header.length = htonl(strlen(response.response) + 1);
        send(client_socket, &response.header, sizeof(response.header), 0);
        send(client_socket, response.response, strlen(response.response) + 1, 0);
    }
}

// Function to handle the PULL command
void handle_pull(int client_socket) {
    char filename[FILENAME_SIZE];
    FILE *fp;
    FileDataMessage file_data;
    MessageHeader header;
    ResponseMessage response;

    // Receive filename
    recv(client_socket, filename, header.length, 0);

    fp = fopen(filename, "rb");
    if (fp == NULL) {
        // Send FILE_NOT_FOUND response
        response.header.type = htonl(MSG_RESPONSE);
        strcpy(response.response, "FILE_NOT_FOUND");
        response.header.length = htonl(strlen(response.response) + 1);
        send(client_socket, &response.header, sizeof(response.header), 0);
        send(client_socket, response.response, strlen(response.response) + 1, 0);
        return;
    } else {
        // Send OK response (empty response)
        response.header.type = htonl(MSG_RESPONSE);
        response.response[0] = '\0';
        response.header.length = htonl(1); // Null terminator
        send(client_socket, &response.header, sizeof(response.header), 0);
        send(client_socket, response.response, 1, 0);
    }

    // Send file data messages
    while ((file_data.header.length = fread(file_data.data, 1, BUFFER_SIZE, fp)) > 0) {
        file_data.header.type = htonl(MSG_FILE_DATA);
        uint32_t length = htonl(file_data.header.length);
        send(client_socket, &file_data.header.type, sizeof(file_data.header.type), 0);
        send(client_socket, &length, sizeof(length), 0);
        send(client_socket, file_data.data, file_data.header.length, 0);
    }

    fclose(fp);
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
