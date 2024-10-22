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
#include "../messages.h"     // Header file with message structs

#define PORT 8080

// Function prototypes
void list_files(int sock);
void diff_files(int sock);
void pull_files(int sock, const char *filename);
void compute_file_md5(const char *filename, char *md5_str);

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char command[BUFFER_SIZE];
    if (chdir("client") != 0) {
        perror("Failed to change directory to server");
        exit(EXIT_FAILURE);
    }
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
            header.type = htonl(MSG_DIFF);
            header.length = htonl(0);
            send(sock, &header, sizeof(header), 0);
            diff_files(sock);
        } else if (strcasecmp(command, "PULL") == 0) {
            printf("Enter the filename to pull: ");
            fgets(command, BUFFER_SIZE, stdin);
            command[strcspn(command, "\n")] = 0; // Remove newline

            // Send PULL command header
            header.type = htonl(MSG_PULL);
            header.length = htonl(strlen(command) + 1); // Include null terminator
            send(sock, &header, sizeof(header), 0);

            // Send filename
            send(sock, command, strlen(command) + 1, 0);

            pull_files(sock, command);
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

// Function to list files from the server
void list_files(int sock) {
    MessageHeader header;
    ResponseMessage response;
    int bytes_read;

    // Receive header
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

    // Receive file list
    bytes_read = recv(sock, response.response, header.length, 0);
    if (bytes_read > 0) {
        printf("Files on server:\n%s", response.response);
    } else {
        printf("Failed to receive file list.\n");
    }
}

// client.c

// Function to perform DIFF operation
void diff_files(int sock) {
    DIR *d;
    struct dirent *dir;
    char md5_hash[MD5_HASH_SIZE];
    MessageHeader header;

    d = opendir("client_files");
    if (d) {
        // For each file in the client's directory
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_REG) {  // Regular files only
                // Compute MD5 of the client file
                char fullpath[FILENAME_SIZE];
                snprintf(fullpath, FILENAME_SIZE, "client_files/%s", dir->d_name);
                compute_file_md5(fullpath, md5_hash);

                // Skip files that don't exist or have an empty MD5 hash
                if (strlen(md5_hash) == 0) {
                    continue;
                }

                // Send MD5 hash to the server
                header.type = htonl(MSG_MD5);
                header.length = htonl(strlen(md5_hash) + 1);  // Include null terminator
                send(sock, &header, sizeof(header), 0);
                send(sock, md5_hash, strlen(md5_hash) + 1, 0);
            }
        }
        closedir(d);
    }

    // Send an end message to signal the end of file transmission
    header.type = htonl(MSG_DONE);
    header.length = htonl(0);
    send(sock, &header, sizeof(header), 0);

    // Receive missing or different files from the server
    printf("Files on the server but not on the client:\n");
    while (1) {
        // Receive the response header
        int bytes_read = recv(sock, &header, sizeof(header), 0);
        if (bytes_read <= 0) break;

        header.type = ntohl(header.type);
        header.length = ntohl(header.length);

        if (header.type == MSG_DONE) {
            // End of DIFF process
            printf("No more files.\n");
            break;
        }

        if (header.type == MSG_RESPONSE) {
            // Receive the filename of a missing or different file
            ResponseMessage response;
            recv(sock, response.response, header.length, 0);
            printf("%s\n", response.response);
        }
    }
}


// Function to perform PULL operation
void pull_files(int sock, const char *filename) {
    FILE *fp;
    FileDataMessage file_data;
    MessageHeader header;
    int bytes_received;

    // Receive header
    bytes_received = recv(sock, &header, sizeof(header), 0);
    if (bytes_received <= 0) {
        printf("Failed to receive response from server.\n");
        return;
    }

    header.type = ntohl(header.type);
    header.length = ntohl(header.length);

    if (header.type == MSG_RESPONSE) {
        ResponseMessage response;
        recv(sock, response.response, header.length, 0);

        if (strncmp(response.response, "FILE_NOT_FOUND", 14) == 0) {
            printf("File not found on server.\n");
            return;
        }
    }

    // Create a new file to write the data
    fp = fopen(filename, "wb");
    if (fp == NULL) {
        printf("Failed to create file '%s'.\n", filename);
        return;
    }

    // Receive file data
    while (1) {
        // Receive header
        bytes_received = recv(sock, &file_data.header, sizeof(file_data.header), 0);
        if (bytes_received <= 0) {
            break;
        }

        file_data.header.type = ntohl(file_data.header.type);
        file_data.header.length = ntohl(file_data.header.length);

        if (file_data.header.type != MSG_FILE_DATA) {
            break;
        }

        // Receive data
        bytes_received = recv(sock, file_data.data, file_data.header.length, 0);
        if (bytes_received <= 0) {
            break;
        }

        fwrite(file_data.data, 1, bytes_received, fp);

        // If the data received is less than expected, break
        if (bytes_received < BUFFER_SIZE) {
            break;
        }
    }

    fclose(fp);
    printf("File '%s' pulled successfully.\n", filename);
}

// Function to compute MD5 hash of a file
void compute_file_md5(const char *filename, char *md5_str) {
    unsigned char c[MD5_DIGEST_LENGTH];
    int i;
    FILE *inFile = fopen(filename, "rb");
    if (inFile == NULL) {
        // If the file is missing, return an empty string as the MD5 hash
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

    // Convert MD5 result to a readable string
    for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf(&md5_str[i * 2], "%02x", c[i]);
    }
    md5_str[32] = '\0';  // Null-terminate the string
    fclose(inFile);
}

