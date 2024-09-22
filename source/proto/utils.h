#ifndef _MFTP_UTILS_H
#define _MFTP_UTILS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#ifdef _WIN32
#error Windows implementation not ready

#include <winsock2.h>
#pragma comment(lib, "Ws2_32.lib")
#include <windows.h>

typedef int sizeret_t;

#else
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>

typedef ssize_t sizeret_t;

#endif

#define log_info(fmt, ...) fprintf(stdout, "[INFO] "fmt"\n", ##__VA_ARGS__)
#define log_err(fmt, ...) fprintf(stderr, "[ERROR] "fmt"\n", ##__VA_ARGS__)
#define log_errno(fmt, ...) fprintf(stderr, "[SYSTEM ERROR] "fmt": (%d) %s\n", errno, strerror(errno));

typedef struct _bnd_sock {
    int sockfd;
    uint32_t addr;
    uint16_t port;
} BoundSocket;

bool boundsocket_init(BoundSocket* bs, uint32_t v4addr, uint16_t port, size_t backlog);
bool boundsocket_init_temp(BoundSocket* bs);
int boundsocket_accept(const BoundSocket* bs);
bool boundsocket_connect(const BoundSocket* bs, const struct sockaddr_in* addr);
void boundsocket_destroy(BoundSocket* bs);

char* read_until(int sockfd, const char* delim, ssize_t* out_size, sig_atomic_t* abort_serv);
void split_by(char* str, char delim, char** first, char** second);
void strip_ws(char** str);
void to_lower(char* str);
void to_upper(char* str);

bool path_cd(char* cwd, const char* cd);

#endif
