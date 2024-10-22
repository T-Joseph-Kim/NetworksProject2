#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>
#include <openssl/md5.h>
#include <stdint.h>
#include "../messages.h"

#define PORT 8080
#define MAX_CACHE_FILES 100
#define FILENAME_SIZE 256
#define BUFFER_SIZE 1024

void list_files(int sock);
void diff_files(int sock, const char *directory);
void pull_files(int sock, const char *directory);
void compute_file_md5(const char *filename, char *md5_str);

char missing_files_cache[MAX_CACHE_FILES][FILENAME_SIZE];
int cache_count = 0;

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char command[BUFFER_SIZE];
    char chosen_directory[FILENAME_SIZE];

    printf("Choose a directory (1 for client_files1, 2 for client_files2): ");
    int choice;
    scanf("%d", &choice);
    getchar();

    if (choice == 1) {
        strcpy(chosen_directory, "client_files1");
    } else if (choice == 2) {
        strcpy(chosen_directory, "client_files2");
    } else {
        printf("Invalid choice. Exiting...\n");
        return -1;
    }

    if (chdir("client") != 0) {
        perror("Failed to change directory to client");
        exit(EXIT_FAILURE);
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }

    printf("Connected to server.\n");

    while (1) {
        printf("\nEnter command (LIST, DIFF, PULL, LEAVE): ");
        fgets(command, BUFFER_SIZE, stdin);
        command[strcspn(command, "\n")] = 0;

        MessageHeader header;
        memset(&header, 0, sizeof(header));

        if (strcasecmp(command, "LIST") == 0) {
            header.type = htonl(MSG_LIST);
            header.length = htonl(0);
            send(sock, &header, sizeof(header), 0);
            list_files(sock);
        } else if (strcasecmp(command, "DIFF") == 0) {
            header.type = htonl(MSG_DIFF);
            header.length = htonl(0);
            send(sock, &header, sizeof(header), 0);
            diff_files(sock, chosen_directory);
        } else if (strcasecmp(command, "PULL") == 0) {
            pull_files(sock, chosen_directory);
        } else if (strcasecmp(command, "LEAVE") == 0) {
            header.type = htonl(MSG_LEAVE);
            header.length = htonl(0);
            send(sock, &header, sizeof(header), 0);
            printf("Disconnected from server.\n");
            close(sock);
            break;
        } else {
            printf("Invalid command.\n");
        }
    }

    return 0;
}

void list_files(int sock) {
    MessageHeader header;
    ResponseMessage response;
    int bytes_read;

    bytes_read = recv(sock, &header, sizeof(header), 0);
    if (bytes_read <= 0) {
        printf("Failed to receive file list header.\n");
        return;
    }

    header.type = ntohl(header.type);
    header.length = ntohl(header.length);

    if (header.type != MSG_RESPONSE) {
        printf("Invalid response from server.\n");
        return;
    }

    bytes_read = recv(sock, response.response, header.length, 0);
    if (bytes_read > 0) {
        printf("Files on server:\n%s", response.response);
    } else {
        printf("Failed to receive file list.\n");
    }
}

void diff_files(int sock, const char *directory) {
    DIR *d;
    struct dirent *dir_entry;
    char md5_hash[MD5_HASH_SIZE];
    MessageHeader header;

    d = opendir(directory);
    if (d) {
        while ((dir_entry = readdir(d)) != NULL) {
            if (dir_entry->d_type == DT_REG) {
                char fullpath[FILENAME_SIZE * 2];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", directory, dir_entry->d_name);
                compute_file_md5(fullpath, md5_hash);

                if (strlen(md5_hash) == 0) {
                    continue;
                }

                header.type = htonl(MSG_MD5);
                header.length = htonl(strlen(md5_hash) + 1);
                send(sock, &header, sizeof(header), 0);
                send(sock, md5_hash, strlen(md5_hash) + 1, 0);
            }
        }
        closedir(d);
    } else {
        printf("Failed to open directory '%s'\n", directory);
        return;
    }

    header.type = htonl(MSG_DONE);
    header.length = htonl(0);
    send(sock, &header, sizeof(header), 0);

    printf("Files on the server but not on the client:\n");
    while (1) {
        int bytes_read = recv(sock, &header, sizeof(header), 0);
        if (bytes_read <= 0) break;

        header.type = ntohl(header.type);
        header.length = ntohl(header.length);

        if (header.type == MSG_DONE) {
            printf("No more files.\n");
            break;
        }

        if (header.type == MSG_RESPONSE) {
            ResponseMessage response;
            recv(sock, response.response, header.length, 0);
            printf("%s\n", response.response);

            int file_exists_in_cache = 0;
            for (int i = 0; i < cache_count; i++) {
                if (strncmp(missing_files_cache[i], response.response, FILENAME_SIZE) == 0) {
                    file_exists_in_cache = 1;
                    break;
                }
            }

            if (!file_exists_in_cache) {
                if (cache_count < MAX_CACHE_FILES) {
                    strncpy(missing_files_cache[cache_count], response.response, FILENAME_SIZE);
                    cache_count++;
                } else {
                    printf("Cache full, unable to store more missing files.\n");
                    break;
                }
            } else {
                printf("File '%s' is already in the cache, skipping.\n", response.response);
            }
        }
    }
}

void pull_files(int sock, const char *directory) {
    for (int i = 0; i < cache_count; i++) {
        const char *filename = missing_files_cache[i];

        MessageHeader header;
        header.type = htonl(MSG_PULL);
        header.length = htonl(strlen(filename) + 1);
        send(sock, &header, sizeof(header), 0);

        send(sock, filename, strlen(filename) + 1, 0);

        char filepath[FILENAME_SIZE * 2];
        snprintf(filepath, sizeof(filepath), "%s/%s", directory, filename);

        FILE *fp = fopen(filepath, "wb");
        if (fp == NULL) {
            printf("Failed to create file '%s'.\n", filepath);
            continue;
        }

        FileDataMessage file_data;
        int bytes_received;
        while (1) {
            bytes_received = recv(sock, &file_data.header, sizeof(file_data.header), 0);
            if (bytes_received <= 0) {
                printf("Connection lost or error while receiving file data.\n");
                break;
            }

            file_data.header.type = ntohl(file_data.header.type);
            file_data.header.length = ntohl(file_data.header.length);

            if (file_data.header.type == MSG_DONE) {
                printf("File transfer completed for '%s'.\n", filename);
                break;
            } else if (file_data.header.type != MSG_FILE_DATA) {
                printf("Unexpected message type received.\n");
                break;
            }

            bytes_received = recv(sock, file_data.data, file_data.header.length, 0);
            if (bytes_received <= 0) {
                printf("Error receiving file data for '%s'.\n", filename);
                break;
            }

            fwrite(file_data.data, 1, bytes_received, fp);
        }

        fclose(fp);
        printf("File '%s' pulled successfully.\n", filename);
    }

    cache_count = 0;
}

void compute_file_md5(const char *filename, char *md5_str) {
    unsigned char c[MD5_DIGEST_LENGTH];
    int i;
    FILE *inFile = fopen(filename, "rb");
    if (inFile == NULL) {
        strcpy(md5_str, "");
        return;
    }

    MD5_CTX mdContext;
    int bytes;
    unsigned char data[BUFFER_SIZE];

    MD5_Init(&mdContext);
    while ((bytes = fread(data, 1, BUFFER_SIZE, inFile)) != 0) {
        MD5_Update(&mdContext, data, bytes);
    }
    MD5_Final(c, &mdContext);

    for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf(&md5_str[i * 2], "%02x", c[i]);
    }
    md5_str[32] = '\0';
    fclose(inFile);
}
