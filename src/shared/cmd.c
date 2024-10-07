#include <string.h>
#include <stdbool.h>

#include <unistd.h>

#include "cmd.h"

#include <assert.h>
#include <stdlib.h>

#include "shared/utils.h"

const char* mftp_cmd_strings[] = {
    "LIST",
    "RETR",
    "STOR",
    "DELE",
    "RMDR",
    "MKDR",
    "CHWD",
    "SIZE",
    "USER",
    "PASS",
    "WAMI",
    "QUIT",
    "RNME",
    "NOOP",
    "ABOR",
    "MDTM",
    "FEAT",
    "PWDR"
};

const char* mftp_ctoa(mftp_cmd_t cmd) {
    return mftp_cmd_strings[cmd];
}

mftp_cmd_t mftp_atoc(const char* cmd) {
    for (int i = 0; i < sizeof(mftp_cmd_strings) / sizeof(mftp_cmd_strings[0]); i++) {
        if (strcmp(mftp_cmd_strings[i], cmd) == 0) {
            return i;
        }
    }
    return MFTP_CMD_INVALID;
}

bool mftp_server_msg_write(int fd, mftp_server_msg_t* msg) {
    char buf[512];
    int len = snprintf(buf, sizeof(buf), "%s %d %s\r\n", msg->kind == MFTP_MSG_OK ? "OK" : "ERROR", msg->code, msg->data);
    if (len < 0) {
        log_syserr("Failed to format server message");
        return false;
    }
    if (write(fd, buf, len) != len) {
        log_syserr("Failed to write server message");
        return false;
    }
    return true;
}

bool mftp_client_msg_parse(const char* buffer, mftp_client_msg_t* msg) {
    assert(buffer != NULL);
    assert(msg != NULL);

    char* buffer_clone = malloc(strlen(buffer) + 1);
    if (buffer_clone == NULL) {
        log_syserr("Failed to allocate memory for buffer clone");
        return false;
    }
    memcpy(buffer_clone, buffer, strlen(buffer) + 1);

    strip(&buffer_clone);

    char* token = strtok(buffer_clone, " ");
    if (!token) {
        free(buffer_clone);
        return false;
    }

    to_upper(token);

    mftp_cmd_t cmd = mftp_atoc(token);
    if (cmd == MFTP_CMD_INVALID) {
        free(buffer_clone);
        return false;
    }

    token = strtok(NULL, "\0");
    if (token && strlen(token) > sizeof(msg->data) - 1) {
        free(buffer_clone);
        return false;
    }

    msg->cmd = cmd;
    if (token) {
        strcpy(msg->data, token);
    }

    free(buffer_clone);
    return true;
}
