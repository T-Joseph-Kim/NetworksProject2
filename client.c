// client.c - Windows Version

#define _WIN32_WINNT 0x0501  // Targeting Windows XP or later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <tchar.h>      // For _TCHAR support
#include <wincrypt.h>   // For Cryptographic API

//#pragma comment(lib, "Ws2_32.lib")  // Not needed with MinGW

#define PORT "8080"
#define BUFFER_SIZE 1024

// Function prototypes
void list_files(SOCKET sock);
void diff_files(SOCKET sock);
void pull_files(SOCKET sock);
void compute_file_md5(const char *filename, char *md5_str);

int main() {
    WSADATA wsaData;
    SOCKET sock = INVALID_SOCKET;
    struct addrinfo hints, *res;
    char command[BUFFER_SIZE];

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }

    // Resolve the server address and port
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;       // IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP

    if (getaddrinfo("127.0.0.1", PORT, &hints, &res) != 0) {
        printf("getaddrinfo failed.\n");
        WSACleanup();
        return 1;
    }

    // Create a SOCKET for connecting to server
    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET) {
        printf("Socket creation error\n");
        freeaddrinfo(res);
        WSACleanup();
        return 1;
    }

    // Connect to server
    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
        printf("Connection Failed\n");
        closesocket(sock);
        freeaddrinfo(res);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(res);

    printf("Connected to server.\n");

    // Command loop
    while (1) {
        printf("\nEnter command (LIST, DIFF, PULL, LEAVE): ");
        fgets(command, BUFFER_SIZE, stdin);
        command[strcspn(command, "\n")] = 0; // Remove newline

        if (_stricmp(command, "LIST") == 0) {
            send(sock, "LIST", strlen("LIST"), 0);
            list_files(sock);
        } else if (_stricmp(command, "DIFF") == 0) {
            send(sock, "DIFF", strlen("DIFF"), 0);
            diff_files(sock);
        } else if (_stricmp(command, "PULL") == 0) {
            send(sock, "PULL", strlen("PULL"), 0);
            pull_files(sock);
        } else if (_stricmp(command, "LEAVE") == 0) {
            send(sock, "LEAVE", strlen("LEAVE"), 0);
            printf("Disconnected from server.\n");
            closesocket(sock);
            break;
        } else {
            printf("Invalid command.\n");
        }
    }

    // Cleanup
    WSACleanup();

    return 0;
}

// Function to list files from the server
void list_files(SOCKET sock) {
    char buffer[BUFFER_SIZE];
    int bytes_read;

    memset(buffer, 0, BUFFER_SIZE);
    bytes_read = recv(sock, buffer, BUFFER_SIZE, 0);
    if (bytes_read > 0) {
        printf("Files on server:\n%s", buffer);
    } else {
        printf("Failed to receive file list.\n");
    }
}

// Function to perform DIFF operation
void diff_files(SOCKET sock) {
    WIN32_FIND_DATA findFileData;
    HANDLE hFind;
    char filename[BUFFER_SIZE];
    char md5_hash[33]; // 32 chars + null terminator
    char server_response[BUFFER_SIZE];

    hFind = FindFirstFileA("*.*", &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("FindFirstFile failed (%lu)\n", GetLastError());
        return;
    }

    do {
        if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            strcpy(filename, findFileData.cFileName);
            compute_file_md5(filename, md5_hash);

            // Send filename and MD5 to server
            send(sock, filename, BUFFER_SIZE, 0);
            send(sock, md5_hash, BUFFER_SIZE, 0);

            // Receive server's response
            recv(sock, server_response, BUFFER_SIZE, 0);
            if (strncmp(server_response, "DIFFERENT", 9) == 0) {
                printf("File '%s' is different on the server.\n", filename);
            } else {
                printf("File '%s' is the same on the server.\n", filename);
            }
        }
    } while (FindNextFileA(hFind, &findFileData) != 0);

    FindClose(hFind);
}

// Function to perform PULL operation
void pull_files(SOCKET sock) {
    char filename[BUFFER_SIZE];
    FILE *fp;
    char file_buffer[BUFFER_SIZE];
    int bytes_read;

    printf("Enter the filename to pull: ");
    fgets(filename, BUFFER_SIZE, stdin);
    filename[strcspn(filename, "\n")] = 0; // Remove newline

    // Send the filename to the server
    send(sock, filename, BUFFER_SIZE, 0);

    // Receive response
    bytes_read = recv(sock, file_buffer, BUFFER_SIZE, 0);
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
        bytes_read = recv(sock, file_buffer, BUFFER_SIZE, 0);
    } while (bytes_read > 0);

    fclose(fp);
    printf("File '%s' pulled successfully.\n", filename);
}

// Function to compute MD5 hash of a file using Windows CryptoAPI
void compute_file_md5(const char *filename, char *md5_str) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE hash[16];
    DWORD hashLen = 16;
    BYTE buffer[BUFFER_SIZE];
    DWORD bytesRead;
    HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

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
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            strcpy(md5_str, "");
            return;
        }
    }

    if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
        for (DWORD i = 0; i < hashLen; i++) {
            sprintf(&md5_str[i * 2], "%02x", hash[i]);
        }
        md5_str[32] = '\0'; // Null-terminate the string
    } else {
        strcpy(md5_str, "");
    }

    CloseHandle(hFile);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
}
