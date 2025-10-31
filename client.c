#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "common.h"

#define PORT 8080
#define SERVER_IP "127.0.0.1"

int main() {
    int sock_fd;
    struct sockaddr_in server_addr;
    char send_buf[MAX_CLIENT_MSG], recv_buf[MAX_CLIENT_MSG];
    int n;

    // Create socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Connect to server
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(1);
    }

    printf("Connected to Online Banking Server.\n");

    // Continuous interaction with server
    while (1) {
        memset(recv_buf, 0, sizeof(recv_buf));
        n = read(sock_fd, recv_buf, sizeof(recv_buf) - 1);
        if (n <= 0) break; // server closed connection
        recv_buf[n] = '\0';
        printf("%s", recv_buf);

        // If prompt expected
        if (strstr(recv_buf, "Choice:") || strstr(recv_buf, "Enter") || strstr(recv_buf, "password")) {
            printf("> ");
            fflush(stdout);
            memset(send_buf, 0, sizeof(send_buf));
            fgets(send_buf, sizeof(send_buf), stdin);
            // Remove newline
            send_buf[strcspn(send_buf, "\n")] = '\0';
            write(sock_fd, send_buf, strlen(send_buf));
        }
    }

    printf("\nDisconnected from server.\n");
    close(sock_fd);
    return 0;
}




//note:
/*
All user data persisted in data/accounts.dat file.

Server handles multiple clients concurrently

All I/O through system calls only

Supports admin, normal user, and joint account types

*/

