#include "passwd.h"

#include "utils.h"
#include "list.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

bool passwd_parse(passwd_t* passwd, const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        log_syserr("Failed to open file %s", filename);
        return false;
    }

    char line[PASSWD_STRING_SIZE * 3];
    size_t line_num = 1;

    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == ';' || strlen(line) < 2) {
            line_num++;
            continue;
        }

        // strtok has weird quirks - it's better that we parse this ourselves

        char* username = NULL;
        char* password = NULL;
        char* perms = NULL;

        char* p = line;
        char* colon;

        // Extract username
        colon = strchr(p, ':');
        if (colon) {
            *colon = '\0';
            username = p;
            p = colon + 1;
        } else {
            username = p;
            p = NULL;
        }

        // Extract password
        if (p) {
            colon = strchr(p, ':');
            if (colon) {
                *colon = '\0';
                password = p;
                p = colon + 1;
            } else {
                password = p;
                p = NULL;
            }
        }

        // Extract permissions
        if (p) {
            perms = p;
            size_t len = strlen(perms);
            if (len > 0 && perms[len - 1] == '\n') {
                perms[len - 1] = '\0';
            }
        }

        // password can be empty - meaning no password

        passwd_entry_t* entry = malloc(sizeof(passwd_entry_t));
        if (entry == NULL) {
            log_syserr("Failed to allocate memory for passwd entry");
            fclose(file);
            return false;
        }

        strncpy(entry->username, username, PASSWD_STRING_SIZE);
        entry->password ? strncpy(entry->password, password, PASSWD_STRING_SIZE) : strcpy(entry->password, "");
        entry->perms = str_to_perm(perms);

        list_insert(&passwd->entries, entry, LIST_BACK);
        line_num++;
    }

    fclose(file);

    return true;
}

void passwd_cleanup(passwd_t* passwd) {
    if (!passwd) return;

    // list_iter_t iter = list_iter(&passwd->entries);
    // passwd_entry_t* entry;

    // while ((entry = list_next(&iter)) != NULL) {
    //     free(entry);
    // }
    list_clear(&passwd->entries);
}

uint8_t passwd_check(const passwd_t* passwd, const char* username, const char* password) {
    list_iter_t iter = list_iter(&passwd->entries);
    passwd_entry_t* entry;

    while ((entry = list_next(&iter)) != NULL) {
        if (strcmp(entry->username, username) == 0 && strcmp(entry->password, password) == 0) {
            return entry->perms;
        }
    }

    return 0;
}

const char* perm_to_str(uint8_t perms) {
    static char str[5] = { 0 };

    str[0] = (perms & PERM_READ) ? 'r' : '-';
    str[1] = (perms & PERM_WRITE) ? 'w' : '-';
    str[2] = (perms & PERM_LIST) ? 'l' : '-';
    str[3] = (perms & PERM_DELETE) ? 'd' : '-';
    str[4] = '\0';

    return str;
}

uint8_t str_to_perm(const char* str) {
    uint8_t perms = 0;

    for (const char* c = str; *c; c++) {
        switch (*c) {
            case 'r': perms |= PERM_READ; break;
            case 'w': perms |= PERM_WRITE; break;
            case 'l': perms |= PERM_LIST; break;
            case 'd': perms |= PERM_DELETE; break;
            default:
                log_err("Invalid permission character: %c", *c);
                return 0;
        }
    }

    return perms;
}
