#ifndef _MFTP_SERVER_CTX_H_
#define _MFTP_SERVER_CTX_H_

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include <uev.h>

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
    uev_t* client_data_watchers; // always cfg.max_clients long
    size_t client_data_watchers_count;
} mftp_server_ctx_t;

uev_t* mftp_server_find_next_active_client_data_watcher(const mftp_server_ctx_t* server_ctx);
uev_t* mftp_server_find_next_empty_client_data_watcher(const mftp_server_ctx_t* server_ctx);
void mftp_server_add_client_data_watcher(mftp_server_ctx_t* server_ctx, uev_t* watcher);
void mftp_server_remove_client_data_watcher(mftp_server_ctx_t* server_ctx, uev_t* watcher);
void mftp_server_cleanup_full(mftp_server_ctx_t* server_ctx);

typedef struct {
    // command channel context:
    int cmd_fd;
    char* cmd_buf;  // always cfg.max_cmd_size bytes long
    size_t cmd_buf_len;
    pthread_t cmd_tid;
    uev_t* cmd_watcher;
    // command channel can't time out
    bool cmd_cleanup_in_progress;

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
    bool t_cleanup_in_progress;

    // general state:
    char* cwd;
} mftp_client_ctx_t;

bool client_ctx_init(mftp_client_ctx_t* ctx, int cmd_fd, mftp_server_ctx_t* server_ctx);
void client_ctx_cleanup_transfer(mftp_client_ctx_t* ctx);
void client_ctx_cleanup_full(mftp_client_ctx_t* ctx);

#endif
