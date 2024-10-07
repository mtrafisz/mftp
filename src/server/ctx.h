#ifndef _MFTP_SERVER_CTX_H_
#define _MFTP_SERVER_CTX_H_

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include <uev.h>

#include "shared/list.h"

typedef struct {
    char username[32];
    char passwd[32];
    char perms[32]; // 'r' - read, 'w' - write, 'd' - delete, 'l' - list,
} mftp_creds_t;

typedef struct {
    struct {
        uint32_t allow_anonymous: 1;
    } flags;
    uint16_t port;
    char *root_dir;
    uint16_t max_clients;
    size_t max_cmd_size;
    uint32_t timeout_ms;
} mftp_server_cfg_t;

typedef struct {
    uev_ctx_t* loop;
    mftp_server_cfg_t cfg;
    int fd;
    mftp_creds_t* user_creds;
    size_t user_creds_count;
    list_t client_data_watchers;
} mftp_server_ctx_t;

void mftp_server_remove_client_data_watcher(mftp_server_ctx_t* server_ctx, uev_t* watcher);
void mftp_server_cleanup_full(mftp_server_ctx_t* server_ctx);

typedef struct {
    // command channel context:
    int cmd_fd;
    char* cmd_buf;  // always cfg.max_cmd_size bytes long
    size_t cmd_buf_len;
    pthread_t cmd_tid;
    uev_t* cmd_watcher;

    // server context:
    mftp_server_ctx_t *server_ctx;

    // authentication:
    bool authenticated;
    mftp_creds_t creds;

    // transfer channel context:
    int t_kind;  // transfer kind - MFTP_CMD_RETR, MFTP_CMD_STOR or MFTP_CMD_LIST
    int t_fd_in, t_fd_out;
    bool t_active;
    pthread_t t_tid;
    uev_t* t_watcher;
    uev_t* t_timeout_watcher;

    // general state:
    char cwd[PATH_MAX]; // relative to server_ctx->cfg.root_dir
    bool locked; // some process is already using this context (it may be cleaned up) - abort whatever you want to do.
} mftp_client_ctx_t;

bool client_ctx_init(mftp_client_ctx_t* ctx, int cmd_fd, mftp_server_ctx_t* server_ctx);
void client_ctx_cleanup_transfer(mftp_client_ctx_t* ctx);
void client_ctx_cleanup_full(mftp_client_ctx_t* ctx);

#endif
