#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "proto/mftp.h"

sig_atomic_t running = true;
void handle_sigpipe(int sig) {
    fprintf(stderr, "server disconnected; exiting...\n");
    running = false;
}

// Data structure to pass information to the threads
typedef struct {
    int sockfd;
} thread_data_t;

void* send_data(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    int sockfd = data->sockfd;

    char c;
    size_t buffer_sz = 0, buffer_cp = 512;
    char* buffer = malloc(buffer_cp);

    while (running) {
        while (true) {
            c = getchar();
            if (c == EOF) break;

            if (buffer_sz == buffer_cp) {
                buffer_cp += 512;
                buffer = realloc(buffer, buffer_cp);
            }

            if (c == '\n') {
                if (buffer_sz + 2 > buffer_cp) {
                    buffer_cp += 512;
                    buffer = realloc(buffer, buffer_cp);
                }
                buffer[buffer_sz++] = '\r';
                buffer[buffer_sz++] = '\n';
                break;  // Send on newline
            } else {
                buffer[buffer_sz++] = c;
            }
        }

        if (write(sockfd, buffer, buffer_sz) == -1) {
            switch (errno) {
                case EPIPE:
                case EINTR:
                    running = false;
                default:
                    break;
            }
            perror("send()");
            free(buffer);
            return NULL;
        }

        memset(buffer, 0, buffer_cp);
        buffer_sz = 0;
    }

    free(buffer);
    return NULL;
}

void* receive_data(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    int sockfd = data->sockfd;
    char buffer[512];

    while (running) {
        int recv_bytes = read(sockfd, buffer, sizeof(buffer) - 1);
        if (recv_bytes <= 0) {
            if (recv_bytes < 0) perror("recv()");
            running = false;
            return NULL;
        } else if (recv_bytes > 0) {
            buffer[recv_bytes] = '\0';
            printf("> %s", buffer);
        }
    }

    return NULL;
}

int main(int argc, char* argv[]) {
    signal(SIGPIPE, handle_sigpipe);
    int errcode = 0;

    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1) {
        perror("socket()");
        exit(1);
    }

    uint16_t port = MFTP_COMMAND_PORT;

    if (argc == 2) {
        port = atoi(argv[1]);
    }

    struct sockaddr_in serv_addr = {0};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (!inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr.s_addr)) {
        fprintf(stderr, "inet_pton() failed\n");
        errcode = 1;
        goto _exit_sock;
    }

    struct timeval timeout = {0};
    timeout.tv_sec = 3;

    // if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 1 ||
    if (   setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout) == -1) {
        perror("setsockopt()");
        errcode = 1;
        goto _exit_sock;
    }

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof serv_addr) == -1) {
        perror("connect()");
        errcode = 1;
        goto _exit_sock;
    }

    printf(" --- press Ctrl-D to send data --- \n");
    printf(" --- press Ctrl-C to terminate --- \n");

    pthread_t send_thread, recv_thread;
    thread_data_t data = { .sockfd = sockfd };

    // Create threads for sending and receiving data
    pthread_create(&send_thread, NULL, send_data, &data);
    pthread_create(&recv_thread, NULL, receive_data, &data);

    // Wait for both threads to finish
    pthread_join(send_thread, NULL);
    pthread_join(recv_thread, NULL);

_exit_sock:
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    return errcode;
}
