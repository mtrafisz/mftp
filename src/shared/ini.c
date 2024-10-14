#include "ini.h"

#include "allocator.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>

// Helper function to trim whitespace
static ini_section_t* find_section(ini_t* ini, const char* section) {
    list_iter_t iter = list_iter(&ini->sections);
    ini_section_t* current;
    
    while ((current = list_next(&iter)) != NULL) {
        if (strcmp(current->name, section) == 0) {
            return current;
        }
    }
    return NULL;
}

static bool parse_line(ini_t* ini, char* line, ini_section_t** current_section) {
    char* trimmed = trim(line);

    if (*trimmed == '\0' || *trimmed == ';' || *trimmed == '#' || *trimmed == '\n')
        return true;

    if (*trimmed == '[') {
        char* end = strchr(trimmed, ']');
        if (!end) {
            log_err("Could not find closing bracket in section header");
            return false;
        }
        *end = '\0';

        ini_section_t* new_section = malloc(sizeof(ini_section_t));
        if (!new_section) {
            log_syserr("Failed to allocate memory for new section");
            return false;
        }
        strncpy(new_section->name, trimmed + 1, INI_BLOB_SIZE - 1);
        new_section->entries = list_new(ini_entry_t);

        if (!list_insert(&ini->sections, new_section, LIST_BACK)) {
            log_err("Failed to insert new section into list");
            free(new_section);
            return false;
        }

        *current_section = new_section;
        return true;
    }

    if (!*current_section) {
        log_err("No section defined for key-value pair: %s", trimmed);
        return false;
    }

    char* equals = strchr(trimmed, '=');
    if (!equals) {
        log_err("Could not find equals sign in key-value pair");
        return false;
    }

    *equals = '\0';
    char* key = trim(trimmed);
    char* value = trim(equals + 1);

    if (!key[0] || !value[0]) {
        log_err("Empty key or value in key-value pair");
        return false;
    }
    if (strlen(key) >= INI_BLOB_SIZE || strlen(value) >= INI_BLOB_SIZE) {
        log_err("Key or value too long in key-value pair");
        return false;
    }
    if (*value == '"') {
        value++;
        char* end = strchr(value, '"');
        if (!end) {
            log_err("Could not find closing quote in value");
            return false;
        }
        *end = '\0';
    }

    ini_entry_t* entry = malloc(sizeof(ini_entry_t));
    if (!entry) {
        log_syserr("Failed to allocate memory for new entry");
        return false;
    }

    strncpy(entry->name, key, INI_BLOB_SIZE - 1);
    entry->type = INI_STRING;
    strncpy(entry->value.s, value, INI_BLOB_SIZE - 1);

    if (!list_insert(&(*current_section)->entries, entry, LIST_BACK)) {
        log_err("Failed to insert new entry into list");
        free(entry);
        return false;
    }

    return true;
}

bool ini_parse(ini_t* ini, const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        log_syserr("Failed to open file %s", filename);
        return false;
    }

    char line[INI_BLOB_SIZE * 2];
    ini_section_t* current_section = NULL;
    size_t line_num = 1;

    while (fgets(line, sizeof(line), file)) {
        if (!parse_line(ini, line, &current_section)) {
            log_info("On line %lu", line_num);
            fclose(file);
            return false;
        }
        line_num++;
    }

    fclose(file);
    return true;
}

void ini_cleanup(ini_t* ini) {
    if (!ini) return;

    list_iter_t iter = list_iter(&ini->sections);
    ini_section_t* section;
    
    while ((section = list_next(&iter)) != NULL) {
        if (section->entries.size > 0) list_clear(&section->entries);
    }
    list_clear(&ini->sections);
}

const char* ini_get_blob(ini_t* ini, const char* section, const char* name, const char* def) {
    if (!ini) return def;

    ini_section_t* sec = find_section(ini, section);
    if (!sec) return def;

    list_iter_t iter = list_iter(&sec->entries);
    ini_entry_t* entry;

    while ((entry = list_next(&iter)) != NULL) {
        if (strcmp(entry->name, name) == 0) {
            return entry->value.s;
        }
    }

    return def;
}

int ini_get_int(ini_t* ini, const char* section, const char* name, int def) {
    if (!ini) return def;

    const char* blob = ini_get_blob(ini, section, name, NULL);
    if (!blob) return def;

    char* end;
    long result = strtol(blob, &end, 10);
    
    if (end == blob || *end != '\0') {
        log_err("Failed to parse integer from string %s", blob);
        return def;
    }

    return (int)result;
}

bool ini_set_blob(ini_t* ini, const char* section, const char* name, const char* value) {
    if (!ini) return false;

    if (strlen(name) >= INI_BLOB_SIZE || strlen(value) >= INI_BLOB_SIZE) {
        log_err("Key or value too long in key-value pair");
        return false;
    }

    ini_section_t* sec = find_section(ini, section);
    if (!sec) {
        sec = malloc(sizeof(ini_section_t));
        if (!sec) {
            log_syserr("Failed to allocate memory for new section");
            return false;
        }
        strncpy(sec->name, section, INI_BLOB_SIZE - 1);
        sec->entries = list_new(ini_entry_t);

        if (!list_insert(&ini->sections, sec, LIST_BACK)) {
            log_err("Failed to insert new section into list");
            free(sec);
            return false;
        }
    }

    ini_entry_t* entry = malloc(sizeof(ini_entry_t));
    if (!entry) {
        log_syserr("Failed to allocate memory for new entry");
        return false;
    }

    strncpy(entry->name, name, INI_BLOB_SIZE - 1);
    entry->type = INI_STRING;
    strncpy(entry->value.s, value, INI_BLOB_SIZE - 1);

    if (!list_insert(&sec->entries, entry, LIST_BACK)) {
        log_err("Failed to insert new entry into list");
        free(entry);
        return false;
    }

    return true;
}

bool ini_set_int(ini_t* ini, const char* section, const char* name, int value) {
    char buffer[INI_BLOB_SIZE];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return ini_set_blob(ini, section, name, buffer);
}

bool ini_save(ini_t* ini, const char* filename) {
    FILE* file = fopen(filename, "w+");
    if (!file) {
        log_syserr("Failed to open file %s", filename);
        return false;
    }

    list_iter_t iter = list_iter(&ini->sections);
    ini_section_t* section;

    fprintf(file, "; WARNING: File auto-generated by mftp-server\n");

    while ((section = list_next(&iter)) != NULL) {
        fprintf(file, "[%s]\n", section->name);

        list_iter_t entry_iter = list_iter(&section->entries);
        ini_entry_t* entry;

        while ((entry = list_next(&entry_iter)) != NULL) {
            fprintf(file, "%s = %s\n", entry->name, entry->value.s);
        }
    }

    fclose(file);
    return true;
}
