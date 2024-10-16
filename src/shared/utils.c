#include "utils.h"

#include <assert.h>
#include <string.h>
#include <ctype.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "allocator.h"
#include <stdlib.h>
const allocator_t std_allocator = {
    .alloc = malloc,
    .free = free,
    .realloc = realloc
};

#include <stdio.h>
#include <stdarg.h>

log_cfg_t log_cfg = {
    .flags = {
        .color = true,
        .datetime = false,
    },
    .level = LOG_TRACE,
};

typedef struct {
    uint8_t r, g, b;
} color_t;

const color_t log_colors[] = {
    {46, 196, 182},
    {253, 255, 252},
    {255, 159, 28},
    {231, 29, 54},
};

#define MAX_LEVEL_LEN 5

void log_stdout(int level, const char* fmt, ...) {
    if (level < log_cfg.level) {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char level_str[16] = {0};
    switch (level) {
        case LOG_TRACE:   strcpy(level_str, "TRACE"); break;
        case LOG_ERROR:   strcpy(level_str, "ERROR"); break;
        case LOG_WARNING: strcpy(level_str, "WARN");  break;
        case LOG_INFO:    strcpy(level_str, "INFO");  break;
        default:          strcpy(level_str, "UNKNOWN"); break;
    }

    char message[4096] = {0};
    vsnprintf(message, sizeof(message), fmt, args);

    int padding = MAX_LEVEL_LEN - (int)strlen(level_str);

    if (log_cfg.flags.color) {
        // fprintf(stderr, "\033[%d;1m[%s]\033[0m%*s%s\n", 
        //         log_colors[level], level_str, padding + 1, "", message); // for standard ansi colors;
        // for rgb colors:
        fprintf(stderr, "\033[38;2;%d;%d;%dm[%s]\033[0m%*s%s\n", 
                log_colors[level].r, log_colors[level].g, log_colors[level].b, level_str, padding + 1, "", message);
    } else {
        fprintf(stderr, "[%s]%*s%s\n", level_str, padding + 1, "", message);
    }

    va_end(args);
}

void strip(char** str) {
    assert(str && *str);

    const char* whitespace = " \t\n\r\f\v ";
    char* c = *str;

    while (strchr(whitespace, *c) != NULL) {
        c++;
    }

    *str = c;
    c = *str + strlen(*str) - 1;

    while (c >= *str && strchr(whitespace, *c) != NULL) {
        c--;
    }

    *(c+1) = '\0';
}

char* trim(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

void to_upper(char* str) {
    assert(str);

    for (char* c = str; *c; c++) {
        *c = (char)toupper(*c);
    }
}

// resolves path based on current working directory and some relative path (ex "/bar", "../foo/../baz" -> "/baz"). '/' is used as root directory - you can't go above it
bool path_join(char out_path[PATH_MAX], const char* cwd, const char* rel_path) {
    if (strlen(cwd) + strlen(rel_path) + 1 > PATH_MAX) {
        return false;
    }

    char temp_rel_path[PATH_MAX];
    strcpy(temp_rel_path, rel_path);

    if (rel_path[0] == '/') {
        strcpy(out_path, "/");
    } else {
        strcpy(out_path, cwd);
    }

    char* token = strtok(temp_rel_path, "/");
    while (token) {
        if (strcmp(token, ".") == 0) {
            // do nothing
        } else if (strcmp(token, "..") == 0) {
            // check if we are not trying to go above root directory
            if (strcmp(out_path, "/") == 0) {
                return false;
            }

            char* last_slash = strrchr(out_path, '/');
            if (last_slash == out_path) {
                // we are in root directory
                out_path[1] = '\0';
            } else {
                *last_slash = '\0';
            }


        } else {
            if (strlen(out_path) + strlen(token) + 2 > PATH_MAX) {
                return false;
            }
            if (out_path[strlen(out_path) - 1] != '/') {
                strcat(out_path, "/");
            }
            strcat(out_path, token);
        }
        token = strtok(NULL, "/");
    }

    // Remove trailing '/' if present
    size_t len = strlen(out_path);
    if (len > 1 && out_path[len - 1] == '/') {
        out_path[len - 1] = '\0';
    }

    return true;
}

void path_normalize(char path[PATH_MAX]) {
    char temp[PATH_MAX];
    char* c = path;
    char* j = temp;

    // Handle leading "./" separately
    if (strncmp(c, "./", 2) == 0) {
        *j++ = '.';
        *j++ = '/';
        c += 2;
    }

    while (*c) {
        if (*c == '/') {
            // Skip multiple slashes
            while (*c == '/') {
                c++;
            }
            if (j == temp || *(j-1) != '/') {
                *j++ = '/';
            }
        } else if (*c == '.' && (*(c+1) == '/' || *(c+1) == '\0')) {
            // Skip "./"
            c++;
            if (*c == '/') {
                c++;
            }
        } else if (*c == '.' && *(c+1) == '.' && (*(c+2) == '/' || *(c+2) == '\0')) {
            // Handle "../"
            c += 2;
            if (j > temp + 1) {
                j--;
                while (j > temp && *(j-1) != '/') {
                    j--;
                }
            }
        } else if (*c == '\\') {
            // Convert backslashes to forward slashes
            *j++ = '/';
            c++;
        } else {
            *j++ = *c++;
        }
    }

    if (j > temp && *(j-1) == '/') {
        j--;
    }
    *j = '\0';

    strcpy(path, temp);
}

// they are functions in case I want to use some env variables or smth

const char* get_config_path() {
    static char path[PATH_MAX];
    snprintf(path, sizeof(path), "/srv/mftp/mftp.conf");
    return path;
}

const char* get_db_path() {
    static char path[PATH_MAX];
    snprintf(path, sizeof(path), "/srv/mftp/mftp.passwd");
    return path;
}

// bool mkdir_p(const char* path) {
//     char* p = NULL;
//     char* copy = strdup(path);
//     if (!copy) {
//         log_syserr("Failed to allocate memory for path copy");
//         return false;
//     }

//     for (p = strchr(copy + 1, '/'); p; p = strchr(p + 1, '/')) {
//         *p = '\0';
//         if (mkdir(copy, 0755) == -1 && errno != EEXIST) {
//             log_syserr("Failed to create directory %s", copy);
//             free(copy);
//             return false;
//         }
//         *p = '/';
//     }

//     if (mkdir(copy, 0755) == -1 && errno != EEXIST) {
//         log_syserr("Failed to create directory %s", copy);
//         free(copy);
//         return false;
//     }

//     free(copy);
//     return true;
// }
