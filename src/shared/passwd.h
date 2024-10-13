#ifndef _MFTP_SHARED_PASSWD_H_
#define _MFTP_SHARED_PASSWD_H_

// simple parser for custom, /etc/paswswd-like files

#include "list.h"

#include <stdint.h>
#include <stdbool.h>

#define PASSWD_STRING_SIZE 64

enum { PERM_READ = 1, PERM_WRITE = 2, PERM_LIST = 4, PERM_DELETE = 8 };
const char* perm_to_str(uint8_t perms);
uint8_t str_to_perm(const char* str);

typedef struct {
    char username[PASSWD_STRING_SIZE];
    char password[PASSWD_STRING_SIZE];
    uint8_t perms;
} passwd_entry_t;

typedef struct {
    list_t entries;
} passwd_t;

bool passwd_parse(passwd_t* passwd, const char* filename);
void passwd_cleanup(passwd_t* passwd);

uint8_t passwd_check(const passwd_t* passwd, const char* username, const char* password);

bool passwd_save(passwd_t* passwd, const char* filename);

#endif
