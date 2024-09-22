#include "server_msg.h"
#include "utils.h"
#include "config.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>

bool servermsg_write(int sockfd, const ServerMessage* msg) {
    char buffer[MFTP_MSG_DATA_SZ * 2] = { 0 };
    sprintf(buffer, "%s %d %.*s\r\n", (msg->kind == MSG_AOK) ? "AOK" : "ERR", msg->code, (int)strlen(msg->data), msg->data);
    return send(sockfd, buffer, strlen(buffer), 0) > 0;
}

ServerMessage servermsg_read(int ssockfd, const char* buffer, size_t buffer_len) {
    assert(buffer_len < 512);
    char buffer_clone[512];
    memcpy(buffer_clone, buffer, buffer_len);

    char* parts[3] = { 0 };

    parts[0] = strtok(buffer_clone, " ");
    if (!parts[0]) {
        return (ServerMessage) {
            .kind = MSG_KIND_INVALID,
            .code = CODE_INVALID,
            .data = { 0 },
        };
    }

    to_lower(parts[0]);
    strip_ws(&parts[0]);

    int msg_kind = MSG_KIND_INVALID;
    if (strcmp(parts[0], "aok") == 0) msg_kind = MSG_AOK;
    else if (strcmp(parts[0], "err") == 0) msg_kind = MSG_ERR;

    parts[1] = strtok(NULL, " ");
    if (!parts[1]) {
        return (ServerMessage) {
            .kind = msg_kind,
            .code = CODE_INVALID,
            .data = { 0 },
        };
    }

    to_lower(parts[1]);
    strip_ws(&parts[1]);
    int msg_code = atoi(parts[1]);

    parts[2] = strtok(NULL, "\r\n");
    if (!parts[1]) {
        return (ServerMessage) {
            .kind = msg_kind,
            .code = msg_code,
            .data =  { 0 },
        };
    }

    to_lower(parts[2]);
    strip_ws(&parts[2]);

    ServerMessage msg =  {
        .kind = msg_kind,
        .code = msg_code,
        .data = { 0 }
    };

    strcpy(msg.data, parts[2]);

    return msg;
}

void servermsg_reset(ServerMessage* msg) {
    msg->kind = MSG_KIND_INVALID;
    msg->code = CODE_INVALID;
    memset(msg->data, 0, 256);
}
