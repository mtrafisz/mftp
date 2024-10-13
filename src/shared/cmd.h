#ifndef _MFTP_SHARED_CMD_H_
#define _MFTP_SHARED_CMD_H_

#include <stdbool.h>

typedef enum {
    MFTP_CMD_LIST,       // list contents of served directory. WARNING: This command opens data channel;
    MFTP_CMD_RETR,       // retrieve file from served directory. WARNING: This command opens data channel;
    MFTP_CMD_STOR,       // store file to served directory. WARNING: This command opens data channel;
    MFTP_CMD_DELE,       // remove file;
    MFTP_CMD_RMDR,       // remove directory;
    MFTP_CMD_MKDR,       // create directory. WARNING - this calls `rm -rf <name>`, and is not reversible;
    MFTP_CMD_CHWD,       // changes current working directory;
    MFTP_CMD_SIZE,       // get size of file;
    MFTP_CMD_USER,       // begin log-in process - provide username;
    MFTP_CMD_PASS,       // continue log-in process - provide password;
    MFTP_CMD_WAMI,       // get current username of logged in user;
    MFTP_CMD_QUIT,       // close connection;
    MFTP_CMD_RNME,       // rename file or directory;
    MFTP_CMD_NOOP,       // do nothing;
    MFTP_CMD_ABOR,       // close current file transfer;
    MFTP_CMD_MDTM,       // get last modification time;
    MFTP_CMD_FEAT,       // list commands available on the server. WARNING: This command opens data channel;;
    MFTP_CMD_PWDR,       // get current working directory;

    MFTP_CMD_INVALID
} mftp_cmd_t;

extern const char* mftp_cmd_strings[];

const char* mftp_ctoa(mftp_cmd_t cmd);
mftp_cmd_t mftp_atoc(const char* cmd);

typedef enum {
    MFTP_CODE_OPENING_DATA_CHANNEL = 120,
    MFTP_CODE_GENERAL_SUCCESS = 200,
    MFTP_CODE_FS_ACTION_SUCCESS = 210,
    MFTP_CODE_READY = 220,
    MFTP_CODE_SERVICE_CLOSING = 221,
    MFTP_CODE_LOGGED_IN = 230,
    MFTP_CODE_CLOSING_DATA_CHANNEL = 320,
    MFTP_CODE_GENERAL_FAILURE = 400,
    MFTP_CODE_FS_READ_FAILURE = 410,
    MFTP_CODE_FS_WRITE_FAILURE = 411,
    MFTP_CODE_FS_ACTION_FAILURE = 412,
    MFTP_CODE_DATA_CHANNEL_ERROR = 420,
    MFTP_CODE_TRANSFER_ABORTED = 421,
    MFTP_CODE_BUSY = 422,
    MFTP_CODE_FORBIDDEN = 430,
    MFTP_CODE_INVALID_COMMAND = 500,
    MFTP_CODE_EXPECTED_ARGUMENT = 501,
    MFTP_CODE_INVALID_ARGUMENT = 502,
    MFTP_CODE_NOT_IMPLEMENTED = 503,
    MFTP_CODE_UNEXPECTED_COMMAND = 504,
    MFTP_CODE_NOT_LOGGED_IN = 530,
    MFTP_CODE_PROVIDE_PASSWORD = 630,

    MFTP_CODE_INVALID = 0
} mftp_code_t;

typedef struct {
    mftp_cmd_t cmd;
    char data[256];
} mftp_client_msg_t;

bool mftp_client_msg_parse(const char* buffer, mftp_client_msg_t* msg);

enum {
    MFTP_MSG_OK,
    MFTP_MSG_ERR,
};

typedef struct {
    int kind;
    mftp_code_t code;
    char data[256];
} mftp_server_msg_t;

bool mftp_server_msg_write(int fd, mftp_server_msg_t* msg);

#endif
