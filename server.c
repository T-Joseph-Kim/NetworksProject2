// server.c - Windows Version

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>    // For _beginthreadex
#include <tchar.h>      // For _TCHAR support

#pragma comment(lib, "Ws2_32.lib")

#define PORT "8080"
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

// Function prototypes
unsigned __stdcall handle_client(void *client_socket);
void send_file_list(SOCKET client_socket);
void handle_diff(SOCKET client_socket);
void handle_pull(SOCKET client_socket);
void compute_file_md5(const char *filename, char *md5_str);

// Thread function to handle each client
unsigned __stdcall handle_client(void *client_socket) {
    SOCKET sock = *(SOCKET *)client_socket;
    char buffer[BUFFER_SIZE];

    printf("Client connected: socket %d\n", (int)sock);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_read = recv(sock, buffer, BUFFER_SIZE, 0);
        if (bytes_read <= 0) {
            printf("Client disconnected: socket %d\n", (int)sock);
            closesocket(sock);
            break;
        }

        if (strncmp(buffer, "LIST", 4) == 0) {
            send_file_list(sock);
        } else if (strncmp(buffer, "DIFF", 4) == 0) {
            handle_diff(sock);
        } else if (strncmp(buffer, "PULL", 4) == 0) {
            handle_pull(sock);
        } else if (strncmp(buffer, "LEAVE", 5) == 0) {
            printf("Client requested to leave: socket %d\n", (int)sock);
            closesocket(sock);
            break;
        } else {
            char *msg = "Invalid command\n";
            send(sock, msg, strlen(msg), 0);
        }
    }

    free(client_socket);
    _endthreadex(0);
    return 0;
}

// Function to send the list of files in the server's directory
void send_file_list(SOCKET client_socket) {
    WIN32_FIND_DATA findFileData;
    HANDLE hFind;
    char file_list[BUFFER_SIZE] = "";
    char directory_search[BUFFER_SIZE] = "*.*";  // Current directory

    hFind = FindFirstFile(directory_search, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("FindFirstFile failed (%d)\n", GetLastError());
        return;
    }

    do {
        if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            strcat(file_list, findFileData.cFileName);
            strcat(file_list, "\n");
        }
    } while (FindNextFile(hFind, &findFileData) != 0);

    FindClose(hFind);

    send(client_socket, file_list, strlen(file_list), 0);
}

// Function to handle the DIFF command
void handle_diff(SOCKET client_socket) {
    char client_md5[BUFFER_SIZE];
    char server_md5[BUFFER_SIZE];
    char filename[BUFFER_SIZE];

    // Receive the filename and MD5 from the client
    recv(client_socket, filename, BUFFER_SIZE, 0);
    recv(client_socket, client_md5, BUFFER_SIZE, 0);

    // Compute MD5 of the server's file
    compute_file_md5(filename, server_md5);

    // Compare MD5 hashes
    if (strcmp(client_md5, server_md5) != 0) {
        char *msg = "DIFFERENT\n";
        send(client_socket, msg, strlen(msg), 0);
    } else {
        char *msg = "SAME\n";
        send(client_socket, msg, strlen(msg), 0);
    }
}

// Function to handle the PULL command
void handle_pull(SOCKET client_socket) {
    char filename[BUFFER_SIZE];
    FILE *fp;
    char file_buffer[BUFFER_SIZE];
    size_t bytes_read;

    // Receive the filename to send
    recv(client_socket, filename, BUFFER_SIZE, 0);

    fp = fopen(filename, "rb");
    if (fp == NULL) {
        char *msg = "FILE_NOT_FOUND\n";
        send(client_socket, msg, strlen(msg), 0);
        return;
    }

    // Send file contents
    while ((bytes_read = fread(file_buffer, 1, BUFFER_SIZE, fp)) > 0) {
        send(client_socket, file_buffer, bytes_read, 0);
    }

    fclose(fp);
}

// Function to compute MD5 hash of a file using Windows CryptoAPI
void compute_file_md5(const char *filename, char *md5_str) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE hash[16];
    DWORD hashLen = 16;
    BYTE buffer[BUFFER_SIZE];
    DWORD bytesRead;
    HANDLE hFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        strcpy(md5_str, "");
        return;
    }

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        CloseHandle(hFile);
        strcpy(md5_str, "");
        return;
    }

    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        CloseHandle(hFile);
        CryptReleaseContext(hProv, 0);
        strcpy(md5_str, "");
        return;
    }

    while (ReadFile(hFile, buffer, BUFFER_SIZE, &bytesRead, NULL) && bytesRead != 0) {
        if (!CryptHashData(hHash, buffer, bytesRead, 0)) {
            CloseHandle(hFile);
            CryptReleaseContext(hProv, 0);
            CryptDestroyHash(hHash);
            strcpy(md5_str, "");
            return;
        }
    }

    if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
        for (DWORD i = 0; i < hashLen; i++) {
            sprintf(&md5_str[i * 2], "%02x", hash[i]);
        }
    } else {
        strcpy(md5_str, "");
    }

    CloseHandle(hFile);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
}

int main() {
    WSADATA wsaData;
    SOCKET server_socket, client_socket;
    struct addrinfo hints, *res;
    int addrlen;
    HANDLE thread_handle;
    DWORD thread_id;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }

    // Prepare the sockaddr_in structure
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;       // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;     // For wildcard IP address

    if (getaddrinfo(NULL, PORT, &hints, &res) != 0) {
        printf("getaddrinfo failed.\n");
        WSACleanup();
        return 1;
    }

    // Create a SOCKET for the server to listen for client connections
    server_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_socket == INVALID_SOCKET) {
        printf("Socket creation failed.\n");
        freeaddrinfo(res);
        WSACleanup();
        return 1;
    }

    // Bind the socket
    if (bind(server_socket, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
        printf("Bind failed.\n");
        freeaddrinfo(res);
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(res);

    // Listen for incoming connections
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        printf("Listen failed.\n");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    printf("Server started on port %s\n", PORT);
    printf("Waiting for connections...\n");

    while (1) {
        client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET) {
            printf("Accept failed.\n");
            closesocket(server_socket);
            WSACleanup();
            return 1;
        }

        printf("Connection accepted: socket %d\n", (int)client_socket);

        SOCKET *pclient = malloc(sizeof(SOCKET));
        *pclient = client_socket;

        // Create a new thread for each client
        thread_handle = (HANDLE)_beginthreadex(NULL, 0, handle_client, (void *)pclient, 0, &thread_id);
        if (thread_handle == NULL) {
            printf("Could not create thread.\n");
            free(pclient);
            continue;
        }

        CloseHandle(thread_handle);
    }

    // Cleanup
    closesocket(server_socket);
    WSACleanup();

    return 0;
}
