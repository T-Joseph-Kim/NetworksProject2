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
#define MAX_CLIENTS 10

// Function prototypes
void *handle_client(void *client_socket);
void send_file_list(int client_socket);
void handle_diff(int client_socket);
void handle_pull(int client_socket);
void compute_file_md5(const char *filename, char *md5_str);

// Thread function to handle each client
void *handle_client(void *client_socket) {
    int sock = *(int *)client_socket;
    char buffer[BUFFER_SIZE];

    printf("Client connected: socket %d\n", sock);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_read = read(sock, buffer, BUFFER_SIZE);
        if (bytes_read <= 0) {
            printf("Client disconnected: socket %d\n", sock);
            close(sock);
            break;
        }

        if (strncmp(buffer, "LIST", 4) == 0) {
            send_file_list(sock);
        } else if (strncmp(buffer, "DIFF", 4) == 0) {
            handle_diff(sock);
        } else if (strncmp(buffer, "PULL", 4) == 0) {
            handle_pull(sock);
        } else if (strncmp(buffer, "LEAVE", 5) == 0) {
            printf("Client requested to leave: socket %d\n", sock);
            close(sock);
            break;
        } else {
            char *msg = "Invalid command\n";
            write(sock, msg, strlen(msg));
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

    write(client_socket, file_list, strlen(file_list));
}

// Function to handle the DIFF command
void handle_diff(int client_socket) {
    char client_md5[BUFFER_SIZE];
    char server_md5[BUFFER_SIZE];
    char filename[BUFFER_SIZE];

    // Receive the filename and MD5 from the client
    read(client_socket, filename, BUFFER_SIZE);
    read(client_socket, client_md5, BUFFER_SIZE);

    // Compute MD5 of the server's file
    compute_file_md5(filename, server_md5);

    // Compare MD5 hashes
    if (strcmp(client_md5, server_md5) != 0) {
        char *msg = "DIFFERENT\n";
        write(client_socket, msg, strlen(msg));
    } else {
        char *msg = "SAME\n";
        write(client_socket, msg, strlen(msg));
    }
}

// Function to handle the PULL command
void handle_pull(int client_socket) {
    char filename[BUFFER_SIZE];
    FILE *fp;
    char file_buffer[BUFFER_SIZE];
    size_t bytes_read;

    // Receive the filename to send
    read(client_socket, filename, BUFFER_SIZE);

    fp = fopen(filename, "rb");
    if (fp == NULL) {
        char *msg = "FILE_NOT_FOUND\n";
        write(client_socket, msg, strlen(msg));
        return;
    }

    // Send file contents
    while ((bytes_read = fread(file_buffer, 1, BUFFER_SIZE, fp)) > 0) {
        write(client_socket, file_buffer, bytes_read);
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

    while ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen))) {
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
