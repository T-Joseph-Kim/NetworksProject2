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
#include "../messages.h"     // Header file with message structs

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
    if (chdir("server") != 0) {
        perror("Failed to change directory to server");
        exit(EXIT_FAILURE);
    }
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

// Function to send the list of files in the server's specific directory
void send_file_list(int client_socket) {
    DIR *d;
    struct dirent *dir;
    char file_list[BUFFER_SIZE] = "";
    MessageHeader header;

    // Open the specific directory where server files are stored
    d = opendir("server_files"); // Change this to the directory you want
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            // Skip directories and only list regular files
            if (dir->d_type == DT_REG) {
                strcat(file_list, dir->d_name);
                strcat(file_list, "\n"); // Add a newline after each file name
            }
        }
        closedir(d);
    } else {
        strcpy(file_list, "Unable to open directory.\n");
    }

    // Send the file list to the client
    header.type = htonl(MSG_RESPONSE);
    header.length = htonl(strlen(file_list) + 1); // +1 for the null terminator
    send(client_socket, &header, sizeof(header), 0);
    send(client_socket, file_list, strlen(file_list) + 1, 0);
}


// Function to handle the DIFF command
void handle_diff(int client_socket) {
    DIR *d;
    struct dirent *dir;
    char server_md5[MD5_HASH_SIZE];
    MessageHeader header;
    ResponseMessage response;

    // Step 1: Store the client's MD5 hashes in memory
    struct {
        char md5_hash[MD5_HASH_SIZE];
    } client_files[100];  // Adjust size as necessary
    int client_file_count = 0;

    // Collect all MD5 hashes from the client
    while (1) {
        // Receive the client's MD5 hash
        int bytes_read = recv(client_socket, &header, sizeof(header), 0);
        if (bytes_read <= 0) break;

        header.type = ntohl(header.type);
        header.length = ntohl(header.length);

        if (header.type == MSG_DONE) {
            // Client has finished sending files
            break;
        }

        if (header.type == MSG_MD5) {
            // Receive MD5 hash from client
            recv(client_socket, client_files[client_file_count].md5_hash, header.length, 0);
            client_file_count++;  // Increment count of client files
        }
    }

    // Step 2: Compare server files' MD5 hashes to the client's MD5 hashes
    d = opendir("server_files");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) {  // Only consider regular files

                // Compute MD5 for the server file
                char fullpath[FILENAME_SIZE];
                snprintf(fullpath, FILENAME_SIZE, "server_files/%s", dir->d_name);
                compute_file_md5(fullpath, server_md5);

                // Skip files with empty MD5 hashes
                if (strlen(server_md5) == 0) {
                    continue;
                }

                int file_found = 0;  // Flag to indicate if the file (by hash) was found

                // Compare server file's MD5 hash to each client file's MD5 hash
                for (int i = 0; i < client_file_count; i++) {
                    if (strcmp(server_md5, client_files[i].md5_hash) == 0) {
                        file_found = 1;  // File with matching MD5 hash exists on the client
                        break;
                    }
                }

                // If the file is not found (by MD5), notify the client
                if (!file_found) {
                    response.header.type = htonl(MSG_RESPONSE);
                    strcpy(response.response, dir->d_name);  // Send filename to the client
                    response.header.length = htonl(strlen(response.response) + 1);
                    send(client_socket, &response.header, sizeof(response.header), 0);
                    send(client_socket, response.response, strlen(response.response) + 1, 0);
                }
            }
        }
        closedir(d);
    }

    // Send a message to signal the end of the DIFF process
    header.type = htonl(MSG_DONE);
    header.length = htonl(0);
    send(client_socket, &header, sizeof(header), 0);
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
