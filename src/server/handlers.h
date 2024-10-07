#ifndef _MFTP_SERVER_HANDLERS_H_
#define _MFTP_SERVER_HANDLERS_H_

#include "shared/cmd.h"
#include "server/ctx.h"

typedef struct {
    mftp_client_ctx_t* client_ctx;
    mftp_client_msg_t cmd;
} command_handler_arg_t;

typedef void (*command_handler_fn)(command_handler_arg_t*);

typedef struct {
    mftp_cmd_t cmd;
    command_handler_fn handler;
} command_handler_t;

extern const command_handler_t command_table[];
extern const size_t command_table_size;

#endif
