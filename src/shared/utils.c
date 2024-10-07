#include "utils.h"

#include <assert.h>
#include <string.h>
#include <ctype.h>

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
