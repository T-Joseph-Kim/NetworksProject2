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
            pull_files(sock);
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
            if (cache_count < MAX_CACHE_FILES) {
                strncpy(missing_files_cache[cache_count], response.response, FILENAME_SIZE);
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

        // Buffer to hold the path to the file in the client_files directory
        char filepath[FILENAME_SIZE + 50];  // Add some extra space for the directory path

        // Assuming client_files is a subdirectory in the current working directory
        snprintf(filepath, sizeof(filepath), "client_files/%s", filename);

        // Open the file in write-binary mode (wb) in the client_files directory
        FILE *fp = fopen(filepath, "wb");
        if (fp == NULL) {
            printf("Failed to create file '%s'.\n", filename);
            continue;
        }

        FileDataMessage file_data;
        int bytes_received;
        while (1) {
            // Receive header
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
            
            // Receive file data
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

    // Clear cache after pulling files
    cache_count = 0;
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

