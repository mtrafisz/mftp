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
#include "shared/ini.h"
#include "shared/passwd.h"
#include "shared/list.h"
#include "server/ctx.h"
#include "server/handlers.h"

void term_callback(uev_t *w, void *arg, int events) {
    puts("");
    log_warn("%s (signo %d). Shutting down...", strsignal(w->siginfo.ssi_signo), w->siginfo.ssi_signo);

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

        log_trace("[CLIENT %d] Invalid command: %s", client_ctx->cmd_fd, client_ctx->cmd_buf);

        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto reset_buffer;
    }

    /* Filter out unauthenticated clients */

    if (!client_ctx->authenticated && cmd.cmd != MFTP_CMD_USER && cmd.cmd != MFTP_CMD_PASS && cmd.cmd != MFTP_CMD_QUIT) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_NOT_LOGGED_IN,
            .data = "Not logged in",
        };

        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto reset_buffer;
    }

    log_trace("[CLIENT %d] %s %s", client_ctx->cmd_fd, mftp_ctoa(cmd.cmd), cmd.cmd == MFTP_CMD_PASS ? "********" : cmd.data);

    /* Find and execute command handler */

    bool cmd_implemented = false;

    for (size_t i = 0; i < command_table_size; i++) {
        if (command_table[i].cmd != cmd.cmd) continue;

        cmd_implemented = true;

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

reset_buffer:
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
    if (server_ctx->client_data_watchers.size >= server_ctx->cfg.max_clients) {
        log_warn("Max clients reached - rejecting connection");
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

    uev_t* client_data_watcher = malloc(sizeof(uev_t));

    uev_io_init(w->ctx, client_data_watcher, client_data_callback, client_ctx, client_cmd_fd, UEV_READ);
    list_insert(&server_ctx->client_data_watchers, client_data_watcher, LIST_BACK);

    client_ctx->cmd_watcher = client_data_watcher;

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

const ini_t get_default_config_ini() {
    ini_t config = { list_new(ini_section_t) };
    
    ini_set(&config, "server", "port", 5555);
    ini_set(&config, "server", "root_dir", "/srv/mftp/fs");
    ini_set(&config, "server", "max_clients", 10);
    ini_set(&config, "server", "max_command_size", 256);
    ini_set(&config, "server", "timeout", 5000);
    ini_set(&config, "server.flags", "allow_anonymous", 1);

    return config;
}

mftp_server_cfg_t parse_ini_to_cfg(ini_t* ini) {
    mftp_server_cfg_t cfg = {
        .port = ini_get_int(ini, "server", "port", 5555),
        .root_dir = ini_get(ini, "server", "root_dir", "/srv/mftp"),
        .max_clients = ini_get_int(ini, "server", "max_clients", 10),
        .max_cmd_size = ini_get_int(ini, "server", "max_command_size", 256),
        .timeout_ms = ini_get_int(ini, "server", "timeout", 5000),
        .flags = {
            .allow_anonymous = ini_get_int(ini, "server.flags", "allow_anonymous", 1),
        },
    };

    return cfg;
}

int main(int argc, char* argv[]) {
    log_cfg.level = LOG_TRACE;

    const char* config_path = get_config_path();

    // load config

    log_trace("Loading config from %s", config_path);
    
    ini_t config_ini = { list_new(ini_section_t) };
    if (!ini_parse(&config_ini, config_path)) {
        ini_cleanup(&config_ini);
        config_ini = get_default_config_ini();

        if (errno == ENOENT) {
            log_warn("Config file not found, saving default config to %s", config_path);
            if (!ini_save(&config_ini, config_path)) {
                log_err("Failed to save default config to %s", config_path);
                ini_cleanup(&config_ini);
                return 1;
            }
        } else {
            log_err("Failed to parse config file, falling back to defaults");
        }
    }

    log_trace("Config parsed, converting to server config");

    mftp_server_cfg_t s_cfg = parse_ini_to_cfg(&config_ini);

    log_trace("Config loaded");

    log_trace("Server config:");
    log_trace("  Port: %d", s_cfg.port);
    log_trace("  Root directory: %s", s_cfg.root_dir);
    log_trace("  Max clients: %d", s_cfg.max_clients);
    log_trace("  Max command size: %d", s_cfg.max_cmd_size);
    log_trace("  Timeout: %d ms", s_cfg.timeout_ms);
    log_trace("  Allow anonymous: %s", s_cfg.flags.allow_anonymous ? "yes" : "no");

    // verify root directory

    if (access(s_cfg.root_dir, R_OK | W_OK | X_OK) != 0) {
        log_err("Root directory %s is not accessible", s_cfg.root_dir);
        ini_cleanup(&config_ini);
        return 1;
    }

    // load user accounts

    const char* db_path = get_db_path();

    log_trace("Loading user accounts from %s", db_path);

    passwd_t s_creds = { .entries = list_new(passwd_entry_t) };
    if (!passwd_parse(&s_creds, db_path)) {
        log_err("Failed to parse passwd file, enabling anonymous login");
        s_cfg.flags.allow_anonymous = 1;
    }

    log_trace("Loaded %zu users from passwd file", s_creds.entries.size);

    struct uev_ctx loop;
    uev_init(&loop);

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
        .creds = s_creds,
        .client_data_watchers = list_new(uev_t),
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
    uev_io_stop(&server_watcher);

    passwd_cleanup(&s_creds);
cleanup:
    uev_exit(&loop);
    ini_cleanup(&config_ini);
    log_info("Server stopped");
    return 0;
}
