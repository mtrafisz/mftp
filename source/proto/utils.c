#include "utils.h"
#include "config.h"

#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

bool boundsocket_init(BoundSocket* bs, uint32_t v4addr, uint16_t port, size_t backlog) {
    struct sockaddr_in bindaddr = { 0 };
    bindaddr.sin_family = AF_INET;
    bindaddr.sin_addr.s_addr = htonl(v4addr);
    bindaddr.sin_port = htons(port);

    bs->sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (bs->sockfd == -1) {
        perror("socket()");
        return false;
    }

    struct timeval timeout = { 0 };
    timeout.tv_sec = MFTP_TIMEOUT_SECS;

    if (setsockopt(bs->sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == -1) {
        perror("setsockopt()");
        goto error_close_socket;
    }

    int optval = true;

    if (setsockopt(bs->sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) == -1) {
        perror("setsockopt()");
        goto error_close_socket;
    }

    if (bind(bs->sockfd, (struct sockaddr*)&bindaddr, sizeof bindaddr) == -1) {
        perror("bind()");
        goto error_close_socket;
    }

    if (listen(bs->sockfd, backlog) == -1) {
        perror("bind()");
        goto error_close_socket;
    }

    struct sockaddr_in sin;
    socklen_t sinlen = sizeof sin;
    if (getsockname(bs->sockfd, (struct sockaddr*)&sin, &sinlen) == -1) {
        perror("getsockname()");
        goto error_close_socket;
    }

    bs->addr = ntohl(sin.sin_addr.s_addr);
    bs->port = ntohs(sin.sin_port);

    return true;

error_close_socket:
    close(bs->sockfd);
    return false;
}

bool boundsocket_init_temp(BoundSocket* bs) {
    return boundsocket_init(bs, INADDR_ANY, 0, 1);
}

int boundsocket_accept(const BoundSocket* bs) {
    int result = accept(bs->sockfd, NULL, NULL);
    if (result == -1) return result;

    struct timeval timeout = { 0 };
    timeout.tv_sec = MFTP_TIMEOUT_SECS;

    if (setsockopt(bs->sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == -1) {
        perror("setsockopt()");
        close(result);
        return -1;
    }

    return result;
}

bool boundsocket_connect(const BoundSocket* bs, const struct sockaddr_in* addr) {
    if (connect(bs->sockfd, (struct sockaddr*)addr, sizeof(struct sockaddr_in)) == -1) {
        perror("connect");
        return false;
    }
    return true;
}

void boundsocket_destroy(BoundSocket* bs) {
    shutdown(bs->sockfd, SHUT_RDWR);
    close(bs->sockfd);
    bs->addr = bs->port = 0;
}

char* read_until(int sockfd, const char* delim, ssize_t* out_size, sig_atomic_t* abort_serv) {
    size_t delim_len = strlen(delim);

    size_t dynbuffer_cap = 512;
    size_t dynbuffer_sz = 0;
    char* dynbuffer = malloc(512);
    char buffer[512];

    size_t bytes_read_sum = 0;
    ssize_t bytes_read;

    while (!(*abort_serv)) {
        bytes_read = read(sockfd, buffer, sizeof buffer);

        if (bytes_read == 0) break;     // EOF
        else if (bytes_read == -1) {    // read failed
            switch (errno) {
                case EAGAIN:            // timeout
                    goto _non_fatal_err;
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
_non_fatal_err:
    *out_size = -1;
    free(dynbuffer);
    return NULL;
_fatal_err:
    *out_size = 0;
    free(dynbuffer);
    return NULL;
}

void split_by(char* str, char delim, char** first, char** second) {
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

void strip_ws(char** str) {
    const char* whitespace = "\n\t\r ";

    char* c = *str;
    while (strchr(whitespace, *c)) {
        c++;
    }

    *str = c;

    c = *str + strlen(*str) - 1;

    while (strchr(whitespace, *c)) {
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

void to_upper(char* str) {
    while (*str) {
        if (islower(*str)) {
            *str = toupper(*str);
        }
        str++;
    }
}

bool path_cd(char* cwd_lit, const char* cd_lit) {
    char cwd[256] = {0};
    char* cwd_ptr = cwd;
    char cd[256] = {0};
    strcpy(cd, cd_lit);

    const char* token = strtok((char*)cd, "/");

    if (strcmp(cwd_lit, "./") == 0) {
        cwd_lit[0] = '\0';
    }

    size_t cwd_len = strlen(cwd_lit);
    if (cwd_len > 0 && cwd_lit[cwd_len - 1] == '/') {
        cwd_lit[cwd_len - 1] = '\0';
    }
    strcpy(cwd, cwd_lit);

    while (token != NULL) {
        if (strcmp(token, ".") == 0) {
        } else if (strcmp(token, "..") == 0) {
            cwd_ptr = strrchr(cwd, '/');
            if (cwd_ptr != NULL) {
                *cwd_ptr = '\0';
            } else {
                cwd[0] = '\0';
            }
        } else {
            if (cwd[0] != '\0') {
                strcat(cwd, "/");
            }
            strcat(cwd, token);
        }

        token = strtok(NULL, "/");
    }

    if (cwd[0] == '\0') {
        strcpy(cwd, ".");
    }

    strncpy(cwd_lit, cwd, 256);

    return true;
}