#ifndef _MFTP_SERVER_MSG_H
#define _MFTP_SERVER_MSG_H

#include <stdbool.h>
#include <stddef.h>

enum {
    MSG_AOK,
    MSG_ERR,

    MSG_KIND_INVALID
};

// 1xx - something is in progress, expect more messages;
// 2xx - success message;
// 3xx - information message - carries neither good, nor bad news;
// 4xx - error - request was correct, but couldn't be completed;
// 50x - error - request itself was malformed or not expected now;
// 6xx - more information needed, expected more messages;
// x1x - file system
// x2x - network connection / communication
// x3x - authentication / policy

enum {
    OpeningDataChannel = 120,
    GeneralSuccess = 200,
    FsActionSuccess = 210,
    Ready = 220,
    ServiceClosing = 221,
    LoggedIn = 230,
    ClosingDataChannel = 320,
    GeneralFailure = 400,
    FsReadFailure = 410,
    FsWriteFailure = 411,
    FsActionFailure = 412,
    DataChannelError = 420,
    TransferAborted = 421,
    Forbidden = 430,
    InvalidCommand = 500,
    ExpectedArgument = 501,
    InvalidArgument = 502,
    NotImplemented = 503,
    NotLoggedIn = 530,
    ProvidePassword = 630,

    CODE_INVALID = 0,
};


typedef struct _server_msg {
    int kind;
    int code;
    char data[256];
} ServerMessage;

bool servermsg_write(int csockfd, const ServerMessage* msg);
ServerMessage servermsg_read(int ssockfd, const char* buffer, size_t buffer_len);
void servermsg_reset(ServerMessage* msg);

#endif
