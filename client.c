// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "common.h"

#define SERVER_IP "127.0.0.1"
#define PORT 8080

ssize_t readln(int fd, char *buf, size_t max) {
    ssize_t t = 0;
    while (t < (ssize_t)max-1) {
        char c; ssize_t n = read(fd, &c, 1);
        if (n <= 0) return (t>0) ? t : n;
        if (c == '\n') break;
        buf[t++] = c;
    }
    buf[t] = 0;
    return t;
}

int main() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }
    struct sockaddr_in srv; memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET; srv.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &srv.sin_addr);

    if (connect(s, (struct sockaddr*)&srv, sizeof(srv)) < 0) { perror("connect"); return 1; }
    printf("Connected to server %s:%d\n", SERVER_IP, PORT);

    char buf[1024];
    while (1) {
        ssize_t n = readln(s, buf, sizeof(buf));
        if (n <= 0) { printf("Server disconnected\n"); break; }
        printf("SERVER: %s\n", buf);
        // if server said BYE or ERR or OK and expects commands, we read user input
        printf("> ");
        fflush(stdout);
        if (!fgets(buf, sizeof(buf), stdin)) break;
        // send to server
        write(s, buf, strlen(buf)); // includes newline
        // continue loop
    }

    close(s);
    return 0;
}
