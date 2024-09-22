#include "client_msg.h"
#include "utils.h"
#include "config.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>

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
    "QUIT",
    "RNME",
    "NOOP",
    "ABOR",
    "MDTM",
    "FEAT",
    "PWDR"
};

const char* mftp_ctoa(int cmd) {
    assert(cmd >= LIST && cmd <= PWDR);     // user should not go around passing random ints here.

    return mftp_cmd_strings[cmd];
}

int mftp_atoc(const char* cmd) {
    for (int i = LIST; i < INVALID_COMMAND; ++i) {
        if (strcmp(cmd, mftp_cmd_strings[i]) == 0) return i;
    }
    return INVALID_COMMAND;
}

bool clientmsg_write(int sockfd, const ClientMessage* msg) {
    char buffer[MFTP_MSG_DATA_SZ * 2] = { 0 };
    sprintf(buffer, "%s %.*s", mftp_ctoa(msg->command), (int)strlen(msg->data), msg->data);
    return send(sockfd, buffer, strlen(buffer), 0) > 0;
}

bool clientmsg_read(int sockfd, ClientMessage* msg, sig_atomic_t* abort_serv) {
    size_t msg_buffer_sz = 0;
    char* msg_buffer = read_until(sockfd, "\r\n", &msg_buffer_sz, abort_serv);
    if (msg_buffer == NULL && msg_buffer_sz == 0) { // DISCONNECTED
        msg->command = QUIT;
        memset(msg->data, 0, MFTP_MSG_DATA_SZ);
        return true;
    }

    char *cmd_buffer, *data_buffer;
    split_by(msg_buffer, ' ', &cmd_buffer, &data_buffer);
    // some commands don't accept data
    bool data_present = (data_buffer == NULL);

    // command can't be NULL
    if (cmd_buffer == NULL) {
        free(msg_buffer);
        msg->command = INVALID_COMMAND;
        memset(msg->data, 0, MFTP_MSG_DATA_SZ);
        return false;
    }

    strip_ws(&cmd_buffer);
    to_lower(cmd_buffer);

    msg->command = mftp_atoc(cmd_buffer);
    if (msg->command == INVALID_COMMAND) {
        memset(msg->data, 0, MFTP_MSG_DATA_SZ);
        return false;
    }

    if (data_present) memcpy(msg->data, data_buffer, strlen(data_buffer));

    return true;
}

void clientmsg_reset(ClientMessage* msg) {
    msg->command = INVALID_COMMAND;
    memset(msg->data, 0, MFTP_MSG_DATA_SZ);
}
