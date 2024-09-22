#ifndef _MFTP_CLIENT_MSG_H
#define _MFTP_CLIENT_MSG_H

#include <stdbool.h>
#include <signal.h>

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

    INVALID_COMMAND
};

const char* mftp_ctoa(int cmd);
int mftp_atoc(const char* cmd);

typedef struct __client_msg {
    int command;
    char data[256];
} ClientMessage;

bool clientmsg_write(int csockfd, const ClientMessage* msg);
bool clientmsg_read(int sockfd, ClientMessage* msg, sig_atomic_t* abort_serv);
void clientmsg_reset(ClientMessage* msg);

#endif
