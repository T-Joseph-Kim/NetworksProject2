#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>
#include <openssl/md5.h>

#define PORT 8080
#define BUFFER_SIZE 1024

// Function prototypes
void list_files(int sock);
void diff_files(int sock);
void pull_files(int sock);
void compute_file_md5(const char *filename, char *md5_str);

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char command[BUFFER_SIZE];

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\nSocket creation error\n");
        return -1;
    }

    // Define server address
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 address from text to binary
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported\n");
        return -1;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed\n");
        return -1;
    }

    printf("Connected to server.\n");

    // Command loop
    while (1) {
        printf("\nEnter command (LIST, DIFF, PULL, LEAVE): ");
        fgets(command, BUFFER_SIZE, stdin);
        command[strcspn(command, "\n")] = 0; // Remove newline

        if (strcasecmp(command, "LIST") == 0) {
            write(sock, "LIST", strlen("LIST"));
            list_files(sock);
        } else if (strcasecmp(command, "DIFF") == 0) {
            write(sock, "DIFF", strlen("DIFF"));
            diff_files(sock);
        } else if (strcasecmp(command, "PULL") == 0) {
            write(sock, "PULL", strlen("PULL"));
            pull_files(sock);
        } else if (strcasecmp(command, "LEAVE") == 0) {
            write(sock, "LEAVE", strlen("LEAVE"));
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
    char buffer[BUFFER_SIZE];
    int bytes_read;

    memset(buffer, 0, BUFFER_SIZE);
    bytes_read = read(sock, buffer, BUFFER_SIZE);
    if (bytes_read > 0) {
        printf("Files on server:\n%s", buffer);
    } else {
        printf("Failed to receive file list.\n");
    }
}

// Function to perform DIFF operation
void diff_files(int sock) {
    DIR *d;
    struct dirent *dir;
    char filename[BUFFER_SIZE];
    char md5_hash[MD5_DIGEST_LENGTH * 2 + 1];
    char server_response[BUFFER_SIZE];

    d = opendir(".");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            // Skip directories
            if (dir->d_type == DT_REG) {
                strcpy(filename, dir->d_name);
                compute_file_md5(filename, md5_hash);

                // Send filename and MD5 to server
                write(sock, filename, BUFFER_SIZE);
                write(sock, md5_hash, BUFFER_SIZE);

                // Receive server's response
                read(sock, server_response, BUFFER_SIZE);
                if (strncmp(server_response, "DIFFERENT", 9) == 0) {
                    printf("File '%s' is different on the server.\n", filename);
                } else {
                    printf("File '%s' is the same on the server.\n", filename);
                }
            }
        }
        closedir(d);
    }
}

// Function to perform PULL operation
void pull_files(int sock) {
    char filename[BUFFER_SIZE];
    FILE *fp;
    char file_buffer[BUFFER_SIZE];
    int bytes_read;

    printf("Enter the filename to pull: ");
    fgets(filename, BUFFER_SIZE, stdin);
    filename[strcspn(filename, "\n")] = 0; // Remove newline

    // Send the filename to the server
    write(sock, filename, BUFFER_SIZE);

    // Receive response
    bytes_read = read(sock, file_buffer, BUFFER_SIZE);
    if (strncmp(file_buffer, "FILE_NOT_FOUND", 14) == 0) {
        printf("File not found on server.\n");
        return;
    }

    // Create a new file to write the data
    fp = fopen(filename, "wb");
    if (fp == NULL) {
        printf("Failed to create file '%s'.\n", filename);
        return;
    }

    // Write the received data into the file
    do {
        fwrite(file_buffer, 1, bytes_read, fp);
        bytes_read = read(sock, file_buffer, BUFFER_SIZE);
    } while (bytes_read > 0);

    fclose(fp);
    printf("File '%s' pulled successfully.\n", filename);
}

// Function to compute MD5 hash of a file
void compute_file_md5(const char *filename, char *md5_str) {
    unsigned char c[MD5_DIGEST_LENGTH];
    int i;
    FILE *inFile = fopen(filename, "rb");
    MD5_CTX mdContext;
    int bytes;
    unsigned char data[1024];

    if (inFile == NULL) {
        strcpy(md5_str, "");
        return;
    }

    MD5_Init(&mdContext);
    while ((bytes = fread(data, 1, 1024, inFile)) != 0)
        MD5_Update(&mdContext, data, bytes);
    MD5_Final(c, &mdContext);

    for (i = 0; i < MD5_DIGEST_LENGTH; i++)
        sprintf(&md5_str[i * 2], "%02x", c[i]);

    fclose(inFile);
}
