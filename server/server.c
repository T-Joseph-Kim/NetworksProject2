#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>
#include <openssl/md5.h>
#include <stdint.h>
#include "../messages.h"

#define PORT 8080
#define MAX_CLIENTS 10

void *handle_client(void *client_socket);
void send_file_list(int client_socket);
void handle_diff(int client_socket);
void handle_pull(int client_socket);
void compute_file_md5(const char *filename, char *md5_str);
void handle_pull_request(int client_socket, const char *filename);

int main() {
    int server_fd, new_socket, *client_sock;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    pthread_t thread_id;
    if (chdir("server") != 0) {
        perror("Failed to change directory to server");
        exit(EXIT_FAILURE);
    }
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    memset(address.sin_zero, '\0', sizeof address.sin_zero);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    printf("Server started on port %d\n", PORT);

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen");
        exit(EXIT_FAILURE);
    }

    printf("Waiting for connections...\n");

    while ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) >= 0) {
        printf("Connection accepted: socket %d\n", new_socket);

        client_sock = malloc(1);
        *client_sock = new_socket;

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
                break;
        }
    }

    free(client_socket);
    pthread_exit(NULL);
    return NULL;
}

void send_file_list(int client_socket) {
    DIR *d;
    struct dirent *dir;
    char file_list[BUFFER_SIZE] = "";
    MessageHeader header;

    d = opendir("server_files");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) {
                strcat(file_list, dir->d_name);
                strcat(file_list, "\n");
            }
        }
        closedir(d);
    } else {
        strcpy(file_list, "Unable to open directory.\n");
    }

    header.type = htonl(MSG_RESPONSE);
    header.length = htonl(strlen(file_list) + 1);
    send(client_socket, &header, sizeof(header), 0);
    send(client_socket, file_list, strlen(file_list) + 1, 0);
}

void handle_diff(int client_socket) {
    DIR *d;
    struct dirent *dir;
    char server_md5[MD5_HASH_SIZE];
    MessageHeader header;
    ResponseMessage response;

    struct {
        char md5_hash[MD5_HASH_SIZE];
    } client_files[100];
    int client_file_count = 0;

    while (1) {
        int bytes_read = recv(client_socket, &header, sizeof(header), 0);
        if (bytes_read <= 0) break;

        header.type = ntohl(header.type);
        header.length = ntohl(header.length);

        if (header.type == MSG_DONE) {
            break;
        }

        if (header.type == MSG_MD5) {
            recv(client_socket, client_files[client_file_count].md5_hash, header.length, 0);
            client_file_count++;
        }
    }

    d = opendir("server_files");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) {

                char fullpath[FILENAME_SIZE];
                snprintf(fullpath, FILENAME_SIZE, "server_files/%s", dir->d_name);
                compute_file_md5(fullpath, server_md5);

                if (strlen(server_md5) == 0) {
                    continue;
                }

                int file_found = 0;

                for (int i = 0; i < client_file_count; i++) {
                    if (strcmp(server_md5, client_files[i].md5_hash) == 0) {
                        file_found = 1;
                        break;
                    }
                }

                if (!file_found) {
                    response.header.type = htonl(MSG_RESPONSE);
                    strcpy(response.response, dir->d_name);
                    response.header.length = htonl(strlen(response.response) + 1);
                    send(client_socket, &response.header, sizeof(response.header), 0);
                    send(client_socket, response.response, strlen(response.response) + 1, 0);
                }
            }
        }
        closedir(d);
    }

    header.type = htonl(MSG_DONE);
    header.length = htonl(0);
    send(client_socket, &header, sizeof(header), 0);
}

void handle_pull(int client_socket) {
    char filename[FILENAME_SIZE];
    recv(client_socket, filename, FILENAME_SIZE, 0);

    handle_pull_request(client_socket, filename);
}

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

    md5_str[32] = '\0';
    fclose(inFile);
}

void handle_pull_request(int client_socket, const char *filename) {
    char filepath[FILENAME_SIZE + 50];

    snprintf(filepath, sizeof(filepath), "server_files/%s", filename);

    FILE *fp = fopen(filepath, "rb");
    if (fp == NULL) {
        printf("File '%s' not found on server.\n", filename);
        return;
    }

    FileDataMessage file_data;
    int bytes_read;
    while ((bytes_read = fread(file_data.data, 1, BUFFER_SIZE, fp)) > 0) {
        file_data.header.type = htonl(MSG_FILE_DATA);
        file_data.header.length = htonl(bytes_read);

        send(client_socket, &file_data.header, sizeof(file_data.header), 0);

        send(client_socket, file_data.data, bytes_read, 0);
    }

    MessageHeader done_msg;
    done_msg.type = htonl(MSG_DONE);
    done_msg.length = htonl(0);
    send(client_socket, &done_msg, sizeof(done_msg), 0);

    fclose(fp);
    printf("File '%s' sent successfully.\n", filename);
}