#ifndef _MFTP_SHARED_UTILS_H_
#define _MFTP_SHARED_UTILS_H_

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>

#define log_info(fmt, ...) fprintf(stdout, "[LOG][INFO] " fmt "\n", ##__VA_ARGS__);
#define log_err(fmt, ...) fprintf(stderr, "[LOG][ERROR] " fmt "\n", ##__VA_ARGS__);
#define log_syserr(fmt, ...) fprintf(stderr, "[LOG][SYSTEM ERROR] " fmt ": %s (%d)\n", ##__VA_ARGS__, strerror(errno), errno);

typedef void* (*pthread_fn)(void*);

void strip(char** str);
void to_upper(char* str);

// resolves path based on current working directory and some relative path (ex "/bar", "../foo/../baz" -> "/baz"). '/' is used as root directory - you can't go above it
bool path_join(char out_path[PATH_MAX], const char* cwd, const char* rel_path);
// normalizes path (removes redundant slashes, resolves ".." and ".")
void path_normalize(char path[PATH_MAX]);

#endif
