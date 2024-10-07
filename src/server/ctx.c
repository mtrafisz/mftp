#include "ctx.h"

#include "shared/utils.h"
#include "shared/cmd.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <unistd.h>
#include <dirent.h>
#include <sys/socket.h>

void mftp_server_remove_client_data_watcher(mftp_server_ctx_t* server_ctx, uev_t* watcher) {
    log_info("REQUESTED TO REMOVE WATCHER");

    list_iter_t iter = list_iter(&server_ctx->client_data_watchers);
    uev_t* w; int i = 0;

    while ((w = list_next(&iter)) != NULL) {
        if (w->fd == watcher->fd) {
            log_info("FOUND WATCHER TO REMOVE");
            uev_io_stop(w);
            client_ctx_cleanup_full((mftp_client_ctx_t*)w->arg);
            list_remove(&server_ctx->client_data_watchers, i);
            free(w);
            return;
        }
        i++;
    }
}

bool client_ctx_init(mftp_client_ctx_t* ctx, int cmd_fd, mftp_server_ctx_t* server_ctx) {
    /* GENERAL STATE */

    ctx->locked = false;
    strcpy(ctx->cwd, "/");

    /* COMMAND CHANNEL CONTEXT */

    ctx->cmd_fd = cmd_fd;
    
    ctx->cmd_buf = malloc(server_ctx->cfg.max_cmd_size);
    if (ctx->cmd_buf == NULL) {
        log_syserr("Failed to allocate memory for client command buffer");
        return false;
    }

    ctx->cmd_buf_len = 0;
    ctx->cmd_tid = 0;

    ctx->cmd_watcher = NULL; // will point to some uev_t in server_ctx->client_data_watchers list

    /* SERVER CONTEXT */

    ctx->server_ctx = server_ctx;

    /* AUTHENTICATION */

    ctx->authenticated = true;
    ctx->creds = (mftp_creds_t) { .username = "anon", .passwd = "", .perms = "rwdl" };

    if (!server_ctx->cfg.flags.allow_anonymous) ctx->authenticated = false;

    /* DATA CHANNEL CONTEXT */

    ctx->t_fd_in = ctx->t_fd_out = -1;
    ctx->t_kind = MFTP_CMD_INVALID;
    ctx->t_watcher = NULL;
    ctx->t_timeout_watcher = NULL;
    ctx->t_active = false;

    client_ctx_cleanup_transfer(ctx);

    return true;
}

void client_ctx_cleanup_transfer(mftp_client_ctx_t* ctx) {
    if (ctx->locked) return;
    ctx->locked = true;

    ctx->t_kind = MFTP_CMD_INVALID;
    
    if (ctx->t_fd_in >= 0) close(ctx->t_fd_in);
    if (ctx->t_fd_out >= 0) close(ctx->t_fd_out);
    
    ctx->t_fd_in = ctx->t_fd_out = -1;
    
    ctx->t_active = false;
    ctx->t_tid = 0;
    
    if (ctx->t_watcher) {
        uev_io_stop(ctx->t_watcher);
        free(ctx->t_watcher);
    }
    ctx->t_watcher = NULL;
    if (ctx->t_timeout_watcher) {
        uev_timer_stop(ctx->t_timeout_watcher);
        free(ctx->t_timeout_watcher);
    }
    ctx->t_timeout_watcher = NULL;

    ctx->locked = false;
}

void client_ctx_cleanup_full(mftp_client_ctx_t* ctx) {
    log_info("CLEANING UP AFTER CLIENT");

    if (ctx->locked) return;
    ctx->locked = true;

    client_ctx_cleanup_transfer(ctx);

    if (ctx->cmd_fd >= 0) {
        shutdown(ctx->cmd_fd, SHUT_RDWR);
        close(ctx->cmd_fd);
    }

    if (ctx->cmd_buf) {
        free(ctx->cmd_buf);
    }

    free(ctx);
}

void mftp_server_cleanup_full(mftp_server_ctx_t* server_ctx) {
    list_iter_t iter = list_iter(&server_ctx->client_data_watchers);
    uev_t* w;

    while ((w = list_next(&iter)) != NULL) {
        mftp_server_remove_client_data_watcher(server_ctx, w);
    }

    list_clear(&server_ctx->client_data_watchers);

    if (server_ctx->fd >= 0) {
        shutdown(server_ctx->fd, SHUT_RDWR);
        close(server_ctx->fd);
    }

    // server_ctx is assumed to be placed on stack
}
