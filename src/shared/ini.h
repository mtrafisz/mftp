#ifndef _MFTP_SHARED_INI_H_
#define _MFTP_SHARED_INI_H_

#include "list.h"
#include <stdbool.h>

// simple INI file parser

// max size of any value in the INI file
#define INI_BLOB_SIZE 256

enum { INI_STRING, INI_NUMBER, };

typedef struct {
    char name[INI_BLOB_SIZE];
    int type;
    union {
        char s[INI_BLOB_SIZE];
        int n;
    } value;
} ini_entry_t;

typedef struct _ini_section_s {
    char name[INI_BLOB_SIZE];
    list_t entries;
} ini_section_t;

typedef struct {
    list_t sections;
} ini_t;

bool ini_parse(ini_t* ini, const char* filename);
void ini_cleanup(ini_t* ini);

const char* ini_get_blob(ini_t* ini, const char* section, const char* name, const char* def);
int ini_get_int(ini_t* ini, const char* section, const char* name, int def);

#define ini_get(ini, section, name, def) _Generic((def), \
    char*: ini_get_blob, \
    const char*: ini_get_blob, \
    int: ini_get_int, \
    unsigned int: ini_get_int, \
    default: ini_get_blob \
)(ini, section, name, def)

bool ini_set_blob(ini_t* ini, const char* section, const char* name, const char* value);
bool ini_set_int(ini_t* ini, const char* section, const char* name, int value);

#define ini_set(ini, section, name, value) _Generic((value), \
    char*: ini_set_blob, \
    const char*: ini_set_blob, \
    int: ini_set_int, \
    unsigned int: ini_set_int, \
    default: ini_set_blob \
)(ini, section, name, value)

bool ini_save(ini_t* ini, const char* filename);

#endif
