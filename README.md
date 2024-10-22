# UFmyMusic by Joseph Kim and Manas Adepu

This program allows clients to synchronize files with a server by comparing MD5 hashes of files in specified directories. It uses multithreading to handle multiple clients and performs file transfers through socket communication.

Please run the program slowly to avoid any errors.

## Features

- **LIST**: Displays the list of files available on the server.
- **DIFF**: Compares the MD5 hashes of files in the client's chosen directory with the server's files and lists the missing or different files.
- **PULL**: Downloads the missing or different files from the server to the client's directory.
- **LEAVE**: Disconnects the client from the server.

## Prerequisites

- Linux Terminal (or Ubuntu for Windows Users)

## Instructions

### 1. Compiling the Program

1. Open a terminal and clone or download the source code.
2. Ensure you have the necessary dependencies installed (GCC, OpenSSL, and `make`).
3. Run `make` within the root directory (NetworksProject2) to compile 
both server and client.
4. There are many test mp3 files that have the keyword "Same" if files are the same on server and clients (and will not be pulled) even if they have a different file name.

### 2. Running the Server

1. Stay in root directory (NetworksProject2)
2. Run the server: `./server/server`

### 3. Running the Client

1. Open a new terminal window (or tab) for the client.
2. Stay in root directory (NetworksProject2)
3. Run the client: `./client/client`
4. Upon starting the client, you will be prompted to select a directory:
"Choose a directory (1 for client_files1, 2 for client_files2):"
5. After selecting the directory, you can use the available commands:
    LIST: Lists the files on the server.
    DIFF: Compares files in your chosen directory with the server. If a difference is found, fills local cache.
    PULL: Downloads missing or different files from the server, based of file names in local cache. Make sure to run DIFF        before PULL to correctly populate cache.
    LEAVE: Disconnects from the server.

### 4. Multithreading Instructions
This program supports multithreading, allowing multiple clients to connect to the server concurrently. However, when running multiple clients, ensure each client joins a directory before starting another client. This prevents potential issues when both clients try to select directories at the same time.

For example:

First client connects and selects client_files1.
Wait for the first client to join the directory.
Then, start the second client and select client_files2.

### 5. Commands for Client
After connecting, you can type the following commands in the client terminal:

LIST: Display a list of files on the server.
DIFF: Compare files between the server and client directory.
PULL: Download missing or different files from the server.
LEAVE: Disconnect from the server.

### 6. Exiting
You can disconnect a client at any time by typing LEAVE. The server will continue to run and accept new connections until manually stopped.

To stop the server, use Ctrl+C in the server's terminal window.

### Project Directory Structure

NetworksProject2/
├── client/
│   ├── client.c
│   ├── client_files1/
│   ├── client_files2/
├── server/
│   ├── server.c
│   ├── server_files/
├── messages.h
├── Makefile
├── README.md
└── architecture.pdf
