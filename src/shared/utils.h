#ifndef _MFTP_SHARED_UTILS_H_
#define _MFTP_SHARED_UTILS_H_

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

enum {
    LOG_TRACE,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
};

typedef struct {
    struct {
        uint16_t color : 1;
        uint16_t datetime : 1;
    } flags;
    int level;
} log_cfg_t;

extern log_cfg_t log_cfg;

void log_stdout(int level, const char* fmt, ...);
#define log_trace(fmt, ...) log_stdout(LOG_TRACE, fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) log_stdout(LOG_INFO, fmt, ##__VA_ARGS__)
#define log_err(fmt, ...) log_stdout(LOG_ERROR, fmt, ##__VA_ARGS__)
#define log_syserr(fmt, ...) log_stdout(LOG_ERROR, fmt ": %s (%d)", ##__VA_ARGS__, strerror(errno), errno)

// #define log_info(fmt, ...) fprintf(stdout, "[LOG][INFO]  " fmt "\n", ##__VA_ARGS__);
// #define log_err(fmt, ...) fprintf(stderr, "[LOG][ERROR] " fmt "\n", ##__VA_ARGS__);
// #define log_syserr(fmt, ...) fprintf(stderr, "[LOG][SYSTEM ERROR] " fmt ": %s (%d)\n", ##__VA_ARGS__, strerror(errno), errno);

#define in_range(x, a, b) ((x) >= (a) && (x) <= (b))

typedef void* (*pthread_fn)(void*);

void strip(char** str);
char* trim(char* str);
void to_upper(char* str);

// resolves path based on current working directory and some relative path (ex "/bar", "../foo/../baz" -> "/baz"). '/' is used as root directory - you can't go above it
bool path_join(char out_path[PATH_MAX], const char* cwd, const char* rel_path);
// normalizes path (removes redundant slashes, resolves ".." and ".")
void path_normalize(char path[PATH_MAX]);

#endif
