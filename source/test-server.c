#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
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
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>

#include "proto/mftp.h"

#define MFTP_BACKLOG 10

const char* bound_addr = "127.0.0.1";

typedef struct _bnd_sock {
    int sockfd;
    uint32_t addr;
    uint16_t port;
} BoundSocket;

BoundSocket new_bound_socket() {
    BoundSocket s = { 0 };

    struct sockaddr_in sockaddr = { 0 };
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(0);
    sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    s.sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s.sockfd == -1) {
        perror("socket()");
        return (BoundSocket){ 0 };
    }

    struct timeval timeout = {0};
    timeout.tv_sec = MFTP_TIMEOUT_SECS;

    if (setsockopt(s.sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == -1) {
        perror("setsockopt()");
        close(s.sockfd);
        return (BoundSocket){ 0 };
    }

    if (bind(s.sockfd, (struct sockaddr*)&sockaddr, sizeof sockaddr) == -1) {
        perror("bind()");
        close(s.sockfd);
        return (BoundSocket){ 0 };
    }

    if (listen(s.sockfd, 1) == -1) {
        perror("listen()");
        close(s.sockfd);
        return (BoundSocket){ 0 };
    }

    struct sockaddr_in sin;
    socklen_t sinlen = sizeof sin;
    if (getsockname(s.sockfd, (struct sockaddr*)&sin, &sinlen) == -1) {
        perror("getsockname()");
        close(s.sockfd);
    }

    s.addr = sin.sin_addr.s_addr;
    s.port = sin.sin_port;

    return s;
}

sig_atomic_t abort_server = false;
void sigint_handler(int _sig) {
    abort_server = true;
    printf("\nSIGINT received, stopping server...\n");
}

char* read_until(int sockfd, const char* delim, size_t* out_size) {
    size_t delim_len = strlen(delim);

    size_t dynbuffer_cap = 512;
    size_t dynbuffer_sz = 0;
    char* dynbuffer = malloc(512);
    char buffer[512];

    size_t bytes_read_sum = 0;
    ssize_t bytes_read;

    while (!abort_server) {
        bytes_read = read(sockfd, buffer, sizeof buffer);

        if (bytes_read == 0) break;  // EOF
        if (bytes_read == -1) {      // read failed
            switch (errno) {
                case EAGAIN:
                case EINTR:
                    continue;
                default:
                    goto _fatal_err;
            }
        }

        bytes_read_sum += bytes_read;

        if (dynbuffer_sz + bytes_read > dynbuffer_cap) {
            dynbuffer_cap = dynbuffer_sz + bytes_read + 512;
            dynbuffer = realloc(dynbuffer, dynbuffer_cap);
        }

        memcpy(dynbuffer + dynbuffer_sz, buffer, bytes_read);
        dynbuffer_sz += bytes_read;

        if (dynbuffer_sz < delim_len) continue;
        for (size_t i = dynbuffer_sz - bytes_read; i <= dynbuffer_sz - delim_len; i++) {
            if (memcmp(dynbuffer + i, delim, delim_len) == 0) {
                size_t total_size = i + delim_len;
                *out_size = total_size;
                dynbuffer = realloc(dynbuffer, total_size);
                lseek(sockfd, -(dynbuffer_sz - total_size), SEEK_CUR);

                return dynbuffer;
            }
        }
    }

_ok_return:
    *out_size = dynbuffer_sz;
    return dynbuffer;
_fatal_err:
    free(dynbuffer);
    return NULL;
}

void split_by_first(char* str, char delim, char** first, char** second) {
    char* c = str;
    while (*c != delim && c < str + strlen(str)) {
        c++;
    }

    if (c == str + strlen(str)) {
        *first = str;
        *second = NULL;
        return;
    }

    *c = 0;
    c++;

    *first = str;
    *second = c;
}

void strip_all(char** str) {
    char* c = *str;
    while (*c == '\n' || *c == ' ' || *c == '\t' || *c == '\r') {
        c++;
    }

    *str = c;

    c = *str + strlen(*str) - 1;

    while (*c == '\n' || *c == ' ' || *c == '\t' || *c == '\r') {
        c--;
    }

    *++c = 0;
}

void to_lower(char* str) {
    while (*str) {
        if (isupper(*str)) {
            *str = tolower(*str);
        }
        str++;
    }
}

void* client_handler(void* ctx) {
    int csockfd = *(int*)ctx;

    char reply[512] = {0};
    strcat(reply, "220 Welcome to MFTP server v0.0.69\r\n");
    write(csockfd, reply, strlen(reply));
    memset(reply, 0, strlen(reply));

    while (!abort_server) {
        size_t recv_size;
        char* recv_bytes = read_until(csockfd, "\r\n", &recv_size);
        if (recv_size == 0) {
            printf("client %d disconnected\n", csockfd);
            goto close_conn;
        } else {
            printf("received %lu bytes from fd %d\n", recv_size, csockfd);
        }

        if (recv_size < 4) {
            free(recv_bytes);
            continue;
        }

        char *data, *cmd;
        size_t cmd_len;

        split_by_first(recv_bytes, ' ', &cmd, &data);
        if (data) strip_all(&data);
        if (data) to_lower(data);
        strip_all(&cmd);
        to_lower(cmd);

        printf("COMMAND: %s; DATA: %s;\n", cmd, data ? data : "NO DATA");

        int command = -1;

        if (strcmp(cmd, "list") == 0) {
            command = LIST;
        } else if (strcmp(cmd, "size") == 0) {
            command = SIZE;
        } else if (strcmp(cmd, "mkdr") == 0) {
            command = MKDR;
        } else if (strcmp(cmd, "noop") == 0) {
            command = NOOP;
        } else if (strcmp(cmd, "quit") == 0) {
            command = QUIT;
        } else if (strcmp(cmd, "rnme") == 0) {
            command = RNME;
        } else if (strcmp(cmd, "stor") == 0) {
            command = STOR;
        } else if (strcmp(cmd, "retr") == 0) {
            command = RETR;
        } else {
            printf("UNKNOWN COMMAND: %s\n", cmd);
            strcat(reply, "202 Unknown command\r\n");
            write(csockfd, reply, strlen(reply));
            goto free_buff;
        }

        switch (command) {
            case NOOP: {
                strcat("220 Ready\r\n", reply);
                write(csockfd, reply, strlen(reply));
                memset(reply, 0, strlen(reply));
            } break;
            case LIST: {
                DIR* d = opendir(".");
                struct dirent *dir;
                if (!d) {
                    strcat(reply, "550 Couldn't get directory listing\n\r");
                    write(csockfd, reply, strlen(reply));
                    memset(reply, 0, strlen(reply));
                } else {
                    BoundSocket bs = new_bound_socket();
                    if (!bs.addr && !bs.port) {
                        fprintf(stderr, "failed to open data connection!\n");
                        goto free_buff;
                    }

                    sprintf(reply, "425 ([%s] %d) Opening connection\r\n", bound_addr, ntohs(bs.port));
                    write(csockfd, reply, strlen(reply));
                    memset(reply, 0, strlen(reply));

                    struct sockaddr_in caddr;
                    socklen_t clen = sizeof caddr;
                    int cdatasock = accept(bs.sockfd, (struct sockaddr*)&caddr, &clen);
                    if (cdatasock == -1) {
                        perror("accept()");
                        goto close_data_conn;
                    }

                    while ((dir = readdir(d))) {
                        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 1) continue;

                        char* type = dir->d_type == DT_DIR ? "DIRECTORY" : "FILE";  // vsc is tweaking!?
                        strcat(reply, dir->d_name);     // sprintf doesn't like char[256] for some reason
                        sprintf(reply + strlen(reply), "\t%s\r\n", type);
                        write(cdatasock, reply, strlen(reply));
                        memset(reply, 0, strlen(reply));
                    }

                close_data_conn:
                    shutdown(bs.sockfd, SHUT_RDWR);
                    close(bs.sockfd);

                    sprintf(reply, "426 Connection closed\r\n");
                    write(csockfd, reply, strlen(reply));
                    memset(reply, 0, strlen(reply));
                }
            } break;
            case MKDR: {
                if (data == NULL) {
                    strcat(reply, "501 Required name parameter\r\n");
                } else {
                    char buffer[128] = { 0 };
                    sprintf(buffer, "mkdir %s", data);
                    if (system(buffer) != 0) {
                        strcat(reply, "450 Directory creation failed\r\n");
                    } else {
                        strcat(reply, "250 Directory created\r\n");
                    }
                }
                write(csockfd, reply, strlen(reply));
                memset(reply, 0, strlen(reply));
            } break;
            case SIZE: {
                struct stat file_stat = {0};
                if (stat(data, &file_stat) == -1)
                    strcat(reply, "550 File not found\r\n");
                else
                    sprintf(reply, "213 %lu\n\r", file_stat.st_size);

                write(csockfd, reply, strlen(reply));
                memset(reply, 0, strlen(reply));
            } break;
            case QUIT: {
                free(recv_bytes);
                goto close_conn;
            } break;
            case RNME: {
                if (data == NULL) {
                    strcat(reply, "501 Required name parameter\r\n");
                } else {
                    char* from = strtok(data, ":");
                    if (!from) {
                        strcat(reply, "501 Invalid rename argument\r\n");
                        goto send_msg_rnme;
                    }
                    char* to = strtok(NULL, "\0");
                    if (!to) {
                        strcat(reply, "501 Invalid rename argument\r\n");
                        goto send_msg_rnme;
                    }

                    char buffer[128] = { 0 };
                    sprintf(buffer, "mv %s %s", from, to);
                    if (system(buffer) != 0) {
                        strcat(reply, "450 Renaming failed\r\n");
                    } else {
                        strcat(reply, "250 Renaming succeded\r\n");
                    }
                }
            send_msg_rnme:
                write(csockfd, reply, strlen(reply));
                memset(reply, 0, strlen(reply));
            } break;
            case STOR: {
                if (!data) {
                    strcat(reply, "501 Required file-name parameter\r\n");
                } else {
                    FILE* outfp = fopen(data, "wb");
                    if (!outfp) {
                        perror("fopen()");
                        strcat(reply, "550 Couldn't write to file\r\n");
                        write(csockfd, reply, strlen(reply));
                        goto free_buff;
                    }

                    BoundSocket bs = new_bound_socket();
                    if (!bs.addr && !bs.port) {
                        fprintf(stderr, "failed to open data connection!\n");
                        goto close_file_stor;
                    }

                    sprintf(reply, "425 ([%s] %d) Opening connection\r\n", bound_addr, ntohs(bs.port));
                    write(csockfd, reply, strlen(reply));
                    memset(reply, 0, strlen(reply));

                    struct sockaddr_in caddr;
                    socklen_t clen = sizeof caddr;
                    int cdatasock = accept(bs.sockfd, (struct sockaddr*)&caddr, &clen);
                    if (cdatasock == -1) {
                        perror("accept()");
                        goto close_data_conn_stor;
                    }

                    char stabuff[1500] = { 0 };
                    ssize_t bytes_read;

                    while ((bytes_read = read(cdatasock, stabuff, sizeof stabuff)) > 0) {
                        fwrite(stabuff, bytes_read, 1, outfp);
                        memset(stabuff, 0, bytes_read);
                    }

                close_data_conn_stor:
                    shutdown(bs.sockfd, SHUT_RDWR);
                    close(bs.sockfd);

                    sprintf(reply, "426 Connection closed\r\n");
                    write(csockfd, reply, strlen(reply));
                    memset(reply, 0, strlen(reply));
                close_file_stor:
                    fflush(outfp);
                    fclose(outfp);
                }
            } break;
            case RETR: {
                if (!data) {
                    strcat(reply, "501 Required file-name parameter\r\n");
                } else {
                    FILE* infp = fopen(data, "rb");
                    if (!infp) {
                        perror("fopen()");
                        strcat(reply, "550 Couldn't read from file\r\n");
                        write(csockfd, reply, strlen(reply));
                        goto free_buff;
                    }

                    BoundSocket bs = new_bound_socket();
                    if (!bs.addr && !bs.port) {
                        fprintf(stderr, "failed to open data connection!\n");
                        goto close_file_retr;
                    }

                    sprintf(reply, "425 ([%s] %d) Opening connection\r\n", bound_addr, ntohs(bs.port));
                    write(csockfd, reply, strlen(reply));
                    memset(reply, 0, strlen(reply));

                    struct sockaddr_in caddr;
                    socklen_t clen = sizeof caddr;
                    int cdatasock = accept(bs.sockfd, (struct sockaddr*)&caddr, &clen);
                    if (cdatasock == -1) {
                        perror("accept()");
                        goto close_data_conn_retr;
                    }

                    char stabuff[1500] = { 0 };
                    ssize_t bytes_read;

                    while ((bytes_read = fread(stabuff, 1, 1500, infp)) > 0) {
                        write(cdatasock, stabuff, bytes_read);
                        memset(stabuff, 0, bytes_read);
                    }

                close_data_conn_retr:
                    shutdown(bs.sockfd, SHUT_RDWR);
                    close(bs.sockfd);

                    sprintf(reply, "426 Connection closed\r\n");
                    write(csockfd, reply, strlen(reply));
                    memset(reply, 0, strlen(reply));
                close_file_retr:
                    fclose(infp);
                }
            } break;
            default:
                assert(false);
        }

    free_buff:
        free(recv_bytes);
    }

close_conn:
    shutdown(csockfd, SHUT_RDWR);
    close(csockfd);
    free(ctx);
    return NULL;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, sigint_handler);
    int errcode = 0;

    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == -1) {
        perror("socket()");
        exit(1);
    }

    struct sockaddr_in bind_addr = {0};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(MFTP_COMMAND_PORT);
    if (!inet_pton(AF_INET, bound_addr, &bind_addr.sin_addr.s_addr)) {
        fprintf(stderr, "inet_pton() failed\n");
        errcode = 1;
        goto _exit_sock;
    }

    if (bind(sockfd, (struct sockaddr*)&bind_addr, sizeof bind_addr) == -1) {
        perror("bind()");
        errcode = 1;
        goto _exit_sock;
    }

    struct timeval timeout = {0};
    timeout.tv_sec = 3;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == -1 ||
        setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout) == -1) {
        perror("setsockopt()");
        errcode = 1;
        goto _exit_sock;
    }

    if (listen(sockfd, MFTP_BACKLOG) == -1) {
        perror("listen()");
        errcode = 1;
        goto _exit_sock;
    }

    printf("Server running on %s:%d\n", bound_addr, MFTP_COMMAND_PORT);

    while (!abort_server) {
        struct sockaddr_in caddr = {0};
        socklen_t clen = sizeof caddr;

        int csockfd = accept(sockfd, (struct sockaddr*)&caddr, &clen);
        if (csockfd == -1) {
            switch (errno) {
                case EAGAIN:
                case EINTR:
                    continue;
                default:
                    perror("accept()");
                    continue;
            }
        }

        pthread_t client_handle;
        int* client_handle_ctx = malloc(sizeof(int));
        *client_handle_ctx = csockfd;
        pthread_create(&client_handle, NULL, client_handler, client_handle_ctx);
        pthread_detach(client_handle);
    }

_exit_sock:
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    return errcode;
}
