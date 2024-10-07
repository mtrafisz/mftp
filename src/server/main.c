#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#include <unistd.h>
#include <netinet/in.h>

#include <uev.h>

#include "shared/utils.h"
#include "shared/socket.h"
#include "shared/cmd.h"
#include "server/ctx.h"
#include "server/handlers.h"

void term_callback(uev_t *w, void *arg, int events) {
    puts("");
    log_info("%s (signo %d). Shutting down...", strsignal(w->siginfo.ssi_signo), w->siginfo.ssi_signo);

    mftp_server_ctx_t *server_ctx = (mftp_server_ctx_t *)arg;
    mftp_server_cleanup_full(server_ctx);

    uev_exit(w->ctx);
}

void client_data_callback(uev_t *w, void *arg, int events) {
    if (events & UEV_ERROR) {
        log_err("Error on client socket");
        return;
    }

    mftp_client_ctx_t *client_ctx = (mftp_client_ctx_t *)arg;

    /* Read raw data from client */

    char buffer[512] = { 0 };

    while (w->active) {
        ssize_t bytes_read = recv(client_ctx->cmd_fd, buffer, sizeof(buffer), 0);

        if (bytes_read == 0) {
            goto disconnect;
        } else if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // wait for more data
                break;
            }

            log_syserr("Failed to read from client socket");
            goto disconnect;
        }

        char* c = buffer;
        char* j = buffer + 1;

        for (size_t i = 0; i < bytes_read - 1; ++i, ++c, ++j) {
            if (*c != '\r' || *j != '\n') {
                continue;
            }
            // if (*c == '\r' && *j == '\n') {
            *c = '\0';

            size_t msg_len = c - buffer;

            if (msg_len == 0) {
                // empty message
                break;
            }

            if (msg_len + client_ctx->cmd_buf_len > client_ctx->server_ctx->cfg.max_cmd_size) {
                mftp_server_msg_t msg = {
                    .kind = MFTP_MSG_ERR,
                    .code = MFTP_CODE_GENERAL_FAILURE,
                    .data = "Command too long - try again",
                };

                mftp_server_msg_write(client_ctx->cmd_fd, &msg);
                memset(client_ctx->cmd_buf, 0, client_ctx->cmd_buf_len);
                client_ctx->cmd_buf_len = 0;
                break;
            }

            memcpy(client_ctx->cmd_buf + client_ctx->cmd_buf_len, buffer, msg_len);
            client_ctx->cmd_buf_len += msg_len;
            client_ctx->cmd_buf[client_ctx->cmd_buf_len] = '\0';

            goto process;
            // }
        }
    }

    // no full message yet
    return;

process:
    /* Parse and handle command */
    mftp_client_msg_t cmd = { 0 };
    if (!mftp_client_msg_parse(client_ctx->cmd_buf, &cmd)) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_INVALID_COMMAND,
            .data = "Invalid command",
        };

        log_info("Invalid command from client %d: %s", client_ctx->cmd_fd, client_ctx->cmd_buf);

        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        return;
    }

    /* Filter out unauthenticated clients */

    if (!client_ctx->authenticated && cmd.cmd != MFTP_CMD_USER && cmd.cmd != MFTP_CMD_PASS && cmd.cmd != MFTP_CMD_QUIT) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_NOT_LOGGED_IN,
            .data = "Not logged in",
        };

        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        client_ctx->cmd_buf_len = 0;
        return;
    }

    log_info("[CLIENT %d] %s %s", client_ctx->cmd_fd, mftp_ctoa(cmd.cmd), cmd.cmd == MFTP_CMD_PASS ? "********" : cmd.data);

    /* Find and execute command handler */

    bool cmd_implemented = false;

    for (size_t i = 0; i < command_table_size; i++) {
        if (command_table[i].cmd != cmd.cmd) continue;

        cmd_implemented = true;

        // clion cries about leak, but it's freed in the handler.
        command_handler_arg_t* handler_arg = malloc(sizeof(command_handler_arg_t));
        if (handler_arg == NULL) {
            log_syserr("Failed to allocate memory for command handler argument");
            return;
        }

        handler_arg->client_ctx = client_ctx;
        handler_arg->cmd = cmd;

        pthread_create(&client_ctx->cmd_tid, NULL, (pthread_fn)command_table[i].handler, handler_arg);
        pthread_detach(client_ctx->cmd_tid);    // we won't join anything by hand - cleanup is up to the thread.

        break;
    }

    if (!cmd_implemented) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_NOT_IMPLEMENTED,
            .data = "Command not implemented",
        };

        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
    }

    // micro "cleanup" after command:
    // memset(client_ctx->cmd_buf, 0, client_ctx->cmd_buf_len);
    client_ctx->cmd_buf_len = 0;
    return;

disconnect:
    log_info("Client %d disconnected", client_ctx->cmd_fd);
    mftp_server_remove_client_data_watcher(client_ctx->server_ctx, w);
    client_ctx_cleanup_full(client_ctx);
}

void server_accept_callback(uev_t *w, void *arg, int events) {
    if (events & UEV_ERROR) {
        log_err("Error on server socket");
        return;
    }

    mftp_server_ctx_t *server_ctx = (mftp_server_ctx_t *)arg;
    if (server_ctx->client_data_watchers_count >= server_ctx->cfg.max_clients) {
        log_info("Max clients reached - rejecting connection");
        return;
    }

    int client_cmd_fd = accept(server_ctx->fd, NULL, NULL);
    if (client_cmd_fd < 0) {
        log_syserr("Failed to accept client connection");
        return;
    }

    mftp_client_ctx_t* client_ctx = malloc(sizeof(mftp_client_ctx_t));
    if (client_ctx == NULL) {
        log_syserr("Failed to allocate memory for client context");
        close(client_cmd_fd);
        return;
    }

    if (!client_ctx_init(client_ctx, client_cmd_fd, server_ctx)) {
        log_err("Failed to initialize client context");
        free(client_ctx);
        close(client_cmd_fd);
        return;
    }

    uev_io_init(w->ctx, client_ctx->cmd_watcher, client_data_callback, client_ctx, client_cmd_fd, UEV_READ);
    mftp_server_add_client_data_watcher(server_ctx, client_ctx->cmd_watcher);

    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_OK,
        .code = MFTP_CODE_READY,
        .data = "Welcome to MFTP server v0.1.69",
    };

    if (!server_ctx->cfg.flags.allow_anonymous) {
        strcat(msg.data, "\nAnonymous login is disabled. Login with USER and PASS commands to continue.");
    }

    mftp_server_msg_write(client_cmd_fd, &msg);
}

int main(int argc, char* argv[]) {
    struct uev_ctx loop;
    uev_init(&loop);

    // TEMPORARY!!!
    // TODO: read from config file
    mftp_server_cfg_t s_cfg = {
        .port = argc > 1 ? atoi(argv[1]) : 2121,
        .root_dir = "./",
        .max_clients = 10,
        .max_cmd_size = 256,
        .timeout_ms = 5000,
        .flags = { .allow_anonymous = true },
    };
    // TODO: read from some kind of "users" file (maybe use the unix passwd file?)
    mftp_creds_t s_creds[] = {
        { .username = "admin", .passwd = "admin123", .perms = "rwdl" },
        { .username = "user", .passwd = "password", .perms = "rl" },
    };
    const size_t s_creds_count = sizeof(s_creds) / sizeof(s_creds[0]);
    // END TEMPORARY!!!

    socket_t server_socket = { 0 };
    if (!socket_bind_tcp(&server_socket, INADDR_ANY, s_cfg.port)) {
        log_err("Failed to bind server socket");
        return 1;
    }

    if (listen(server_socket.fd, s_cfg.max_clients) < 0) {
        log_syserr("Failed to listen on server socket");
        socket_cleanup(&server_socket);
        return 1;
    }

    mftp_server_ctx_t server_ctx = {
        .loop = &loop,
        .cfg = s_cfg,
        .fd = server_socket.fd,
        .user_creds = s_creds,
        .user_creds_count = s_creds_count,
        .client_data_watchers = malloc(s_cfg.max_clients * sizeof(uev_t)),
        .client_data_watchers_count = 0,
    };

    uev_t server_watcher;
    uev_io_init(&loop, &server_watcher, server_accept_callback, &server_ctx, server_socket.fd, UEV_READ);

    uev_t sigint_watcher, sigterm_watcher;
    uev_signal_init(&loop, &sigint_watcher, term_callback, &server_ctx, SIGINT);
    uev_signal_init(&loop, &sigterm_watcher, term_callback, &server_ctx, SIGTERM);

    log_info("Server started on port %d", s_cfg.port);

    uev_run(&loop, 0);

    uev_signal_stop(&sigint_watcher);
    uev_signal_stop(&sigterm_watcher);
cleanup:
    uev_exit(&loop);
    return 0;
}
