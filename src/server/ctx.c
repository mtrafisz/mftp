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

uev_t* mftp_server_find_next_active_client_data_watcher(const mftp_server_ctx_t* server_ctx) {
    assert(server_ctx != NULL);
    assert(server_ctx->cfg.max_clients > 0);

    if (!server_ctx->client_data_watchers || server_ctx->client_data_watchers_count == 0) return NULL;

    for (size_t i = 0; i < server_ctx->cfg.max_clients; i++) {
        if (server_ctx->client_data_watchers[i].active) {
            return &server_ctx->client_data_watchers[i];
        }
    }
    return NULL;
}

uev_t* mftp_server_find_next_empty_client_data_watcher(const mftp_server_ctx_t* server_ctx) {
    for (size_t i = 0; i < server_ctx->cfg.max_clients; i++) {
        if (!server_ctx->client_data_watchers[i].active) {
            return &server_ctx->client_data_watchers[i];
        }
    }
    return NULL;
}

void mftp_server_add_client_data_watcher(mftp_server_ctx_t* server_ctx, uev_t* watcher) {
    assert(server_ctx->client_data_watchers_count < server_ctx->cfg.max_clients);
    uev_t* next_watcher = mftp_server_find_next_empty_client_data_watcher(server_ctx);
    assert(next_watcher != NULL);

    for (size_t i = 0; i < server_ctx->cfg.max_clients; i++) {
        if (&server_ctx->client_data_watchers[i] == next_watcher) {
            memcpy(next_watcher, watcher, sizeof(uev_t));
            next_watcher->active = true;
            server_ctx->client_data_watchers_count++;
            // log_info("Added client watcher - %lu active clients in total", server_ctx->client_data_watchers_count);
            return;
        }
    }
}

void mftp_server_remove_client_data_watcher(mftp_server_ctx_t* server_ctx, uev_t* watcher) {
    assert(server_ctx->client_data_watchers_count > 0);
    for (size_t i = 0; i < server_ctx->cfg.max_clients; i++) {
        if (&server_ctx->client_data_watchers[i] == watcher) {
            memset(watcher, 0, sizeof(uev_t));
            server_ctx->client_data_watchers_count--;
            return;
        }
    }
}

bool client_ctx_init(mftp_client_ctx_t* ctx, int cmd_fd, mftp_server_ctx_t* server_ctx) {
    ctx->cmd_fd = cmd_fd;
    ctx->cmd_buf = malloc(server_ctx->cfg.max_cmd_size);
    if (ctx->cmd_buf == NULL) {
        log_syserr("Failed to allocate memory for command buffer");
        return false;
    }
    ctx->cmd_buf_len = 0;
    ctx->cmd_tid = 0;
    ctx->cmd_watcher = malloc(sizeof(uev_t));
    if (ctx->cmd_watcher == NULL) {
        log_syserr("Failed to allocate memory for command watcher");
        free(ctx->cmd_buf);
        return false;
    }
    ctx->cmd_cleanup_in_progress = false;

    ctx->server_ctx = server_ctx;

    ctx->authenticated = true;
    ctx->creds = (mftp_creds_t){ .username = "anon", .passwd = "", .perms = "rwld" };
    if (!server_ctx->cfg.flags.allow_anonymous) {
        ctx->authenticated = false;
    }

    ctx->t_kind = -1;
    ctx->t_fd_in = -1;
    ctx->t_fd_out = -1;
    ctx->t_active = false;
    ctx->t_tid = 0;
    ctx->t_watcher = malloc(sizeof(uev_t));
    if (ctx->t_watcher == NULL) {
        log_syserr("Failed to allocate memory for transfer watcher");
        free(ctx->cmd_buf);
        free(ctx->cmd_watcher);
        return false;
    }
    ctx->t_timeout_watcher = malloc(sizeof(uev_t));
    if (ctx->t_timeout_watcher == NULL) {
        log_syserr("Failed to allocate memory for transfer timeout watcher");
        free(ctx->cmd_buf);
        free(ctx->cmd_watcher);
        free(ctx->t_watcher);
        return false;
    }
    ctx->t_cleanup_in_progress = false;

    ctx->cwd = malloc(PATH_MAX);
    strcpy(ctx->cwd, "/");

    return true;
}

void client_ctx_cleanup_transfer(mftp_client_ctx_t* ctx) {
    ctx->t_cleanup_in_progress = true;
    ctx->t_active = false;

    if (ctx->t_watcher != NULL && ctx->t_watcher->active) {
        uev_io_stop(ctx->t_watcher);
    }
    if (ctx->t_timeout_watcher != NULL && ctx->t_timeout_watcher->active) {
        uev_timer_stop(ctx->t_timeout_watcher);
    }

    if (ctx->t_fd_in >= 0) {
        close(ctx->t_fd_in);
    }
    if (ctx->t_fd_out >= 0) {
        close(ctx->t_fd_out);
    }

    ctx->t_kind = -1;
    ctx->t_fd_in = -1;
    ctx->t_fd_out = -1;
    ctx->t_active = false;
    ctx->t_tid = 0;
    ctx->t_cleanup_in_progress = false;
}

void client_ctx_cleanup_full(mftp_client_ctx_t* ctx) {
    assert (ctx);

    ctx->cmd_cleanup_in_progress = true;
    ctx->authenticated = false;

    if (ctx->cmd_watcher != NULL) {
        uev_io_stop(ctx->cmd_watcher);
        // free(ctx->cmd_watcher);
    }

    if (ctx->cmd_fd >= 0) {
        close(ctx->cmd_fd);
    }

    client_ctx_cleanup_transfer(ctx);
    // free(ctx->t_watcher);
    free(ctx->t_timeout_watcher);

    // those checks shouldn't really be necessary, but my cleanup code is a mess...
    if (ctx->cmd_buf != NULL) free(ctx->cmd_buf);
    if (ctx->cwd != NULL) free(ctx->cwd);

    free(ctx);
}

void mftp_server_cleanup_full(mftp_server_ctx_t* server_ctx) {
    for (size_t i = 0; i < server_ctx->cfg.max_clients; i++) {
        uev_t* watcher = mftp_server_find_next_active_client_data_watcher(server_ctx);
        if (watcher == NULL) {
            // log_info("Found and cleaned-up %lu active client data watchers", i);
            break;
        }

        mftp_client_ctx_t* client_ctx = (mftp_client_ctx_t *)watcher->arg;
        if (client_ctx == NULL) {
            log_err("%lu: Client context is NULL", i); // should not happen :)
            continue;
        }
        client_ctx_cleanup_full(client_ctx);
        mftp_server_remove_client_data_watcher(server_ctx, watcher);
    }

    if (server_ctx->fd >= 0) {
        shutdown(server_ctx->fd, SHUT_RDWR);
        close(server_ctx->fd);
    }

    free(server_ctx->client_data_watchers);
}
