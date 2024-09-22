#include "proto/config.h"

#include <stdint.h>
#include <stdio.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("usage: %s <file to dump> <port>", argv[0]);
        return 1;
    }

    const char* file_path = argv[1];
    const char* port_str = argv[2];

    uint16_t port = atoi(port_str);

    FILE* fp = fopen(file_path, "rb");
    if (fp == NULL) {
        perror("fopen() failed");
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1) {
        perror("socket() failed");
        goto close_file;
    }

    struct sockaddr_in servaddr = { 0 };
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    servaddr.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof servaddr) == -1) {
        perror("connect() failed");
        goto close_file;
    }

    char buffer[4096] = { 0 };
    size_t bytes_read = 0;

    while ((bytes_read = fread(buffer, 1, sizeof buffer, fp)) > 0) {
        send(sockfd, buffer, bytes_read, 0);
    }

    printf("DONE!\n");

close_file:
    fclose(fp);
    close(sockfd);
    return 0;
}
