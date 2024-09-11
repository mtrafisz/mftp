#ifndef _MFTP_H
#define _MFTP_H

#define MFTP_COMMAND_PORT 6666
#define MFTP_TIMEOUT_SECS 10

enum MFTP_COMMAND {
    LIST,       // list contents of served directory. WARNING: This command opens data channel;
    RETR,       // retrieve file from served directory. WARNING: This command opens data channel;
    STOR,       // store file to served directory. WARNING: This command opens data channel;
    DELE,       // remove file;
    RMDR,       // remove directory;
    MKDR,       // create directory. WARNING - this calls `rm -rf <name>`, and is not reversable;
    CHWD,       // changes current working directory;
    SIZE,       // get size of file;
    USER,       // begin log-in process - provide username;
    PASS,       // continue log-in process - provide password;
    QUIT,       // close connection;
    RNME,       // rename file or directory;
    NOOP,       // do nothing;
    ABOR,       // close current file transfer;
    MDTM,       // get last modification time;
    FEAT,       // list commands available on the server. WARNING: This command opens data channel;;
    PWDR,       // get current working directory;
};

const char* mftp_cta(int cmd);
int mftp_atc(const char* cmd);

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
};

#endif
