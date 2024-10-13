#include "handlers.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <dirent.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "shared/socket.h"
#include "shared/utils.h"

void* transfer_thread(void* arg) {
    mftp_client_ctx_t* ctx = (mftp_client_ctx_t*)arg;
    ctx->t_active = true;

    char buffer[512] = { 0 };

    switch (ctx->t_kind) {
    case MFTP_CMD_LIST: {
        DIR* cwd = fdopendir(ctx->t_fd_in);
        struct dirent* entry;

        while ((entry = readdir(cwd)) != NULL && ctx->t_active) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            char* entry_type;
            switch (entry->d_type) {
            case DT_DIR:
                entry_type = "DIRECTORY";
                break;
            case DT_REG:
                entry_type = "FILE";
                break;
            default:
                entry_type = "OTHER";
                break;
            }

            sprintf(buffer, "%s\t%s\r\n", entry_type, entry->d_name);
            write(ctx->t_fd_out, buffer, strlen(buffer));
        }

        closedir(cwd);
    } break;
    // A little bit of code duplication, but I think it's fine the way it is - easier to read and modify if needed.
    case MFTP_CMD_RETR: {
        FILE* file = fdopen(ctx->t_fd_in, "rb");

        while (ctx->t_active) {
            size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
            if (bytes_read == 0) break;
            send(ctx->t_fd_out, buffer, bytes_read, 0);
        }

        fclose(file);
    } break;
    case MFTP_CMD_STOR: {
        FILE* file = fdopen(ctx->t_fd_out, "wb");

        while (ctx->t_active) {
            ssize_t bytes_read = recv(ctx->t_fd_in, buffer, sizeof(buffer), 0);
            if (bytes_read == 0) break;
            if (bytes_read < 0) {
                log_syserr("Failed to read from file");
                break;
            }
            fwrite(buffer, 1, bytes_read, file);
        }

        fclose(file);
    } break;
    default:
        assert(false);
        break;
    }

    if (!ctx->t_active) return NULL; // transfer aborted forcefully

    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_OK,
        .code = MFTP_CODE_CLOSING_DATA_CHANNEL,
        .data = "Transfer complete",
    };
     mftp_server_msg_write(ctx->cmd_fd, &msg);

    client_ctx_cleanup_transfer(ctx);
    return NULL;
}

void data_accept_callback(uev_t* w, void* arg, int events) {
    if (events & UEV_ERROR) {
        log_err("Error on client socket");
        return;
    }

    mftp_client_ctx_t* client_ctx = (mftp_client_ctx_t*)arg;
    mftp_server_ctx_t* server_ctx = client_ctx->server_ctx;

    if (client_ctx->locked) return;
    client_ctx->locked = true;
    uev_timer_stop(client_ctx->t_timeout_watcher);
    uev_io_stop(client_ctx->t_watcher);

    int* data_fd_ptr;
    switch (client_ctx->t_kind) {
        case MFTP_CMD_LIST:
        case MFTP_CMD_RETR:
            data_fd_ptr = &client_ctx->t_fd_out;
            break;
        case MFTP_CMD_STOR:
            data_fd_ptr = &client_ctx->t_fd_in;
            break;
        default:
            assert(false);
            break;
    }
    *data_fd_ptr = accept(w->fd, NULL, NULL);
    if (*data_fd_ptr < 0) {
        log_syserr("Failed to accept data connection"); // should not happen - READ was triggered, but no connection can be accepted.
        client_ctx->locked = false;
        return;
    }

    pthread_create(&client_ctx->t_tid, NULL, transfer_thread, client_ctx);
    pthread_detach(client_ctx->t_tid);

    client_ctx->locked = false;

    return;
}

void data_timeout_callback(uev_t* w, void* arg, int events) {
    if (events & UEV_ERROR) {
        log_err("Error on client socket");
        return;
    }

    mftp_client_ctx_t* client_ctx = (mftp_client_ctx_t*)arg;
    if (client_ctx->locked) return;
    client_ctx->locked = true;

    uev_io_stop(client_ctx->t_watcher);
    uev_timer_stop(client_ctx->t_timeout_watcher);

    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_ERR,
        .code = MFTP_CODE_CLOSING_DATA_CHANNEL,
        .data = "Timeout",
    };

    mftp_server_msg_write(client_ctx->cmd_fd, &msg);

    client_ctx->locked = false;

    client_ctx_cleanup_transfer(client_ctx);
}

void mftp_handle_noop(command_handler_arg_t* arg) {
    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_OK,
        .code = MFTP_CODE_READY,
        .data = "Ready",
    };
    mftp_server_msg_write(arg->client_ctx->cmd_fd, &msg);

    free(arg);
}

void mftp_handle_quit(command_handler_arg_t* arg) {
    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_OK,
        .code = MFTP_CODE_SERVICE_CLOSING,
        .data = "Goodbye",
    };
    mftp_server_msg_write(arg->client_ctx->cmd_fd, &msg);

    mftp_server_remove_client_data_watcher(arg->client_ctx->server_ctx, arg->client_ctx->cmd_watcher);

    free(arg);
}

void mftp_handle_feat(command_handler_arg_t* arg) {
    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_OK,
        .code = MFTP_CODE_GENERAL_SUCCESS,
        .data = { 0 },
    };

    for (int i = 0; i < command_table_size - 1; i++) {
        strcat(msg.data, mftp_ctoa(command_table[i].cmd));
        strcat(msg.data, ",");
    }

    strcat(msg.data, mftp_ctoa(command_table[command_table_size - 1].cmd));

    mftp_server_msg_write(arg->client_ctx->cmd_fd, &msg);

    free(arg);
}

void mftp_handle_user(command_handler_arg_t* arg) {
    mftp_client_msg_t cmd = arg->cmd;
    mftp_client_ctx_t* client_ctx = arg->client_ctx;

    size_t arg_len = strlen(cmd.data);

    if (arg_len == 0) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_EXPECTED_ARGUMENT,
            .data = "Username not provided",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    } else if (arg_len > sizeof(client_ctx->creds.username) - 1) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_INVALID_ARGUMENT,
            .data = "Username too long",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    // logout
    client_ctx->authenticated = false;
    memset(client_ctx->creds.username, 0, sizeof(client_ctx->creds.username));
    memset(client_ctx->creds.password, 0, sizeof(client_ctx->creds.password));

    // copy new username
    strncpy(client_ctx->creds.username, cmd.data, strlen(cmd.data));

    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_OK,
        .code = MFTP_CODE_PROVIDE_PASSWORD,
        .data = "Username OK, provide password",
    };
    mftp_server_msg_write(client_ctx->cmd_fd, &msg);

cleanup:
    free(arg);
}

void mftp_handle_pass(command_handler_arg_t* arg) {
    mftp_client_msg_t cmd = arg->cmd;
    mftp_client_ctx_t* client_ctx = arg->client_ctx;
    mftp_server_ctx_t* server_ctx = client_ctx->server_ctx;

    size_t arg_len = strlen(cmd.data);

    // if (arg_len == 0) {
    //     mftp_server_msg_t msg = {
    //         .kind = MFTP_MSG_ERR,
    //         .code = MFTP_CODE_EXPECTED_ARGUMENT,
    //         .data = "Password not provided",
    //     };
    //     mftp_server_msg_write(client_ctx->cmd_fd, &msg);
    //     goto cleanup;
    // } else 
    if (arg_len > sizeof(client_ctx->creds.password) - 1) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_INVALID_ARGUMENT,
            .data = "Password too long",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    } else if (client_ctx->authenticated) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_UNEXPECTED_COMMAND,
            .data = "Already logged in",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    memcpy(client_ctx->creds.password, cmd.data, arg_len);

    bool creds_ok = false;

    struct timeval start_tv, end_tv;
    gettimeofday(&start_tv, NULL);

    client_ctx->creds.perms = passwd_check(&server_ctx->creds, client_ctx->creds.username, client_ctx->creds.password);
    if (client_ctx->creds.perms != 0) {
        creds_ok = true;
    }

    mftp_server_msg_t msg;

    if (creds_ok) {
        client_ctx->authenticated = true;

        msg = (mftp_server_msg_t) {
            .kind = MFTP_MSG_OK,
            .code = MFTP_CODE_LOGGED_IN,
            .data = "Logged in as ",
        };
        strcat(msg.data, client_ctx->creds.username);
    } else if (server_ctx->cfg.flags.allow_anonymous && strcmp(client_ctx->creds.username, "anon") == 0) {
        client_ctx->authenticated = true;
        client_ctx->creds.perms = PERM_LIST | PERM_READ;

        msg = (mftp_server_msg_t) {
            .kind = MFTP_MSG_OK,
            .code = MFTP_CODE_LOGGED_IN,
            .data = "Logged in anonymously",
        };
    } else {
        msg = (mftp_server_msg_t) {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_FORBIDDEN,
            .data = "Invalid credentials",
        };

        // prevent timing attacks
        // Does this even make sense? Ping over the network shouldn't be consistent enough to measure time differences...
        gettimeofday(&end_tv, NULL);
        if (end_tv.tv_sec - start_tv.tv_sec < 1) {
            usleep(1000000 - (end_tv.tv_usec - start_tv.tv_usec));
        }
    }

    mftp_server_msg_write(client_ctx->cmd_fd, &msg);

cleanup:
    free(arg);
}

void mftp_handle_wami(command_handler_arg_t* arg) {
    mftp_client_ctx_t* client_ctx = arg->client_ctx;
    mftp_server_msg_t msg;

    if (!client_ctx->authenticated) {
        msg = (mftp_server_msg_t){
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_NOT_LOGGED_IN,
            .data = "Not logged in",
        };
    } else {
        msg = (mftp_server_msg_t){
            .kind = MFTP_MSG_OK,
            .code = MFTP_CODE_GENERAL_SUCCESS,
            .data = { 0 },
        };
        sprintf(msg.data, "%s %s", client_ctx->creds.username, perm_to_str(client_ctx->creds.perms));
    }

    mftp_server_msg_write(client_ctx->cmd_fd, &msg);

cleanup:
    free(arg);
}

void mftp_handle_list(command_handler_arg_t* arg) {
    mftp_client_ctx_t* client_ctx = arg->client_ctx;

    if (~client_ctx->creds.perms & PERM_LIST) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_FORBIDDEN,
            .data = "Permission denied",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    if (client_ctx->t_active) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_BUSY,
            .data = "Transfer in progress",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    char cwd_full[512] = { 0 };
    sprintf(cwd_full, "%s%s", client_ctx->server_ctx->cfg.root_dir, client_ctx->cwd + 1); // + 1 to skip '/'

    DIR* cwd = opendir(cwd_full);
    if (cwd == NULL) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_FS_READ_FAILURE,
            .data = "Failed to open directory",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    socket_t data_ch_socket = { 0 };
    socket_bind_tcp(&data_ch_socket, INADDR_ANY, 0);

    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_OK,
        .code = MFTP_CODE_OPENING_DATA_CHANNEL,
        .data = { 0 },
    };

    sprintf(msg.data, "[%s:%d] Opening data channel", inet_ntoa((struct in_addr){ .s_addr = data_ch_socket.haddr }), data_ch_socket.hport);
    mftp_server_msg_write(client_ctx->cmd_fd, &msg);

    listen(data_ch_socket.fd, 1);

    client_ctx->t_fd_in = dup(dirfd(cwd)); // I love posix streams :3
    client_ctx->t_kind = MFTP_CMD_LIST;

    closedir(cwd); // not closing this here will leak internal os resources

    client_ctx->t_watcher = malloc(sizeof(uev_t));
    uev_io_init(client_ctx->server_ctx->loop, client_ctx->t_watcher, data_accept_callback, client_ctx, data_ch_socket.fd, UEV_READ);
    client_ctx->t_timeout_watcher = malloc(sizeof(uev_t));
    uev_timer_init(client_ctx->server_ctx->loop, client_ctx->t_timeout_watcher, data_timeout_callback, client_ctx, (int)client_ctx->server_ctx->cfg.timeout_ms, 0);

cleanup:
    free(arg);
}

void mftp_handle_retr(command_handler_arg_t* arg) {
    mftp_client_ctx_t* client_ctx = arg->client_ctx;
    mftp_client_msg_t cmd = arg->cmd;

    if (~client_ctx->creds.perms & PERM_READ) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_FORBIDDEN,
            .data = "Permission denied",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    if (client_ctx->t_active) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_BUSY,
            .data = "Transfer in progress",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    if (strlen(cmd.data) == 0) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_EXPECTED_ARGUMENT,
            .data = "Filename not provided",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    char file_path_full[PATH_MAX] = { 0 };

    sprintf(file_path_full, "%.*s%.*s/", (int)strlen(client_ctx->server_ctx->cfg.root_dir), client_ctx->server_ctx->cfg.root_dir, (int)strlen(client_ctx->cwd), client_ctx->cwd);
    strcat(file_path_full, cmd.data);
    path_normalize(file_path_full);

    FILE* file = fopen(file_path_full, "rb");
    if (file == NULL) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_FS_READ_FAILURE,
            .data = "Failed to open file",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    socket_t data_ch_socket = { 0 };
    socket_bind_tcp(&data_ch_socket, INADDR_ANY, 0);

    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_OK,
        .code = MFTP_CODE_OPENING_DATA_CHANNEL,
        .data = { 0 },
    };

    sprintf(msg.data, "[%s:%d] Opening data channel", inet_ntoa((struct in_addr){ .s_addr = data_ch_socket.haddr }), data_ch_socket.hport);
    mftp_server_msg_write(client_ctx->cmd_fd, &msg);

    listen(data_ch_socket.fd, 1);

    client_ctx->t_fd_in = dup(fileno(file));
    client_ctx->t_kind = MFTP_CMD_RETR;

    fclose(file);

    client_ctx->t_watcher = malloc(sizeof(uev_t));
    uev_io_init(client_ctx->server_ctx->loop, client_ctx->t_watcher, data_accept_callback, client_ctx, data_ch_socket.fd, UEV_READ);
    client_ctx->t_timeout_watcher = malloc(sizeof(uev_t));
    uev_timer_init(client_ctx->server_ctx->loop, client_ctx->t_timeout_watcher, data_timeout_callback, client_ctx, (int)client_ctx->server_ctx->cfg.timeout_ms, 0);

cleanup:
    free(arg);
}

void mftp_handle_stor(command_handler_arg_t* arg) {
    mftp_client_ctx_t* client_ctx = arg->client_ctx;
    mftp_client_msg_t cmd = arg->cmd;

    if (~client_ctx->creds.perms & PERM_WRITE) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_FORBIDDEN,
            .data = "Permission denied",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    if (client_ctx->t_active) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_BUSY,
            .data = "Transfer in progress",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    if (strlen(cmd.data) == 0) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_EXPECTED_ARGUMENT,
            .data = "Filename not provided",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    char file_path_full[PATH_MAX] = { 0 };

    sprintf(file_path_full, "%.*s%.*s/", (int)strlen(client_ctx->server_ctx->cfg.root_dir), client_ctx->server_ctx->cfg.root_dir, (int)strlen(client_ctx->cwd), client_ctx->cwd);
    strcat(file_path_full, cmd.data);
    path_normalize(file_path_full);

    FILE* file = fopen(file_path_full, "wb");
    if (file == NULL) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_FS_WRITE_FAILURE,
            .data = "Failed to open file",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    socket_t data_ch_socket = { 0 };
    socket_bind_tcp(&data_ch_socket, INADDR_ANY, 0);

    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_OK,
        .code = MFTP_CODE_OPENING_DATA_CHANNEL,
        .data = { 0 },
    };

    sprintf(msg.data, "[%s:%d] Opening data channel", inet_ntoa((struct in_addr){ .s_addr = data_ch_socket.haddr }), data_ch_socket.hport);
    mftp_server_msg_write(client_ctx->cmd_fd, &msg);

    listen(data_ch_socket.fd, 1);

    client_ctx->t_fd_out = dup(fileno(file));
    client_ctx->t_kind = MFTP_CMD_STOR;

    fclose(file);

    client_ctx->t_watcher = malloc(sizeof(uev_t));
    uev_io_init(client_ctx->server_ctx->loop, client_ctx->t_watcher, data_accept_callback, client_ctx, data_ch_socket.fd, UEV_READ);
    client_ctx->t_timeout_watcher = malloc(sizeof(uev_t));
    uev_timer_init(client_ctx->server_ctx->loop, client_ctx->t_timeout_watcher, data_timeout_callback, client_ctx, (int)client_ctx->server_ctx->cfg.timeout_ms, 0);

cleanup:
    free(arg);
}

void mftp_handle_pwdr(command_handler_arg_t* arg) {
    mftp_client_ctx_t* client_ctx = arg->client_ctx;

    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_OK,
        .code = MFTP_CODE_GENERAL_SUCCESS,
        .data = { 0 },
    };

    strcat(msg.data, client_ctx->cwd);
    mftp_server_msg_write(client_ctx->cmd_fd, &msg);

    free(arg);
}

void mftp_handle_chwd(command_handler_arg_t* arg) {
    mftp_client_msg_t cmd = arg->cmd;
    mftp_client_ctx_t* client_ctx = arg->client_ctx;

    if (strlen(cmd.data) == 0) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_EXPECTED_ARGUMENT,
            .data = "Path not provided",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    char root_realpath[PATH_MAX] = { 0 }, full_realpath[PATH_MAX] = { 0 }, new_cwd[PATH_MAX] = { 0 };
    realpath(client_ctx->server_ctx->cfg.root_dir, root_realpath);

    if (!path_join(new_cwd, client_ctx->cwd, cmd.data)) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_GENERAL_FAILURE,
            .data = "Path too long",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    if (strlen(new_cwd) + strlen(root_realpath) + 1 > PATH_MAX) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_GENERAL_FAILURE,
            .data = "Path too long",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }
    
    // this abomination suppresses the warning about buffer overflow.
    snprintf(full_realpath, sizeof(full_realpath), "%.*s%.*s", (int)strlen(root_realpath), root_realpath, (int)strlen(new_cwd), new_cwd);
    path_normalize(full_realpath);

    if (access(full_realpath, F_OK) == -1) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_FS_READ_FAILURE,
            .data = "Path does not exist",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    } else if (access(full_realpath, R_OK) == -1) {
        log_syserr("Failed to access path"); // shouldn't happen :)
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_FORBIDDEN,
            .data = "Permission denied",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    memset(client_ctx->cwd, 0, sizeof(client_ctx->cwd));
    strcat(client_ctx->cwd, new_cwd);

    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_OK,
        .code = MFTP_CODE_FS_ACTION_SUCCESS,
        .data = { 0 },
    };
    strcat(msg.data, client_ctx->cwd);

    mftp_server_msg_write(client_ctx->cmd_fd, &msg);

cleanup:
    free(arg);
}

void mftp_handle_dele(command_handler_arg_t* arg) {
    mftp_client_msg_t cmd = arg->cmd;
    mftp_client_ctx_t* client_ctx = arg->client_ctx;

    if (~client_ctx->creds.perms & PERM_WRITE) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_FORBIDDEN,
            .data = "Permission denied",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    if (strlen(cmd.data) == 0) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_EXPECTED_ARGUMENT,
            .data = "Filename not provided",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    char file_path_full[PATH_MAX] = { 0 };
    sprintf(file_path_full, "%.*s%.*s/", (int)strlen(client_ctx->server_ctx->cfg.root_dir), client_ctx->server_ctx->cfg.root_dir, (int)strlen(client_ctx->cwd), client_ctx->cwd);
    strcat(file_path_full, cmd.data);

    if (remove(file_path_full) == -1) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_FS_WRITE_FAILURE,
            .data = "Failed to delete file",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_OK,
        .code = MFTP_CODE_FS_ACTION_SUCCESS,
        .data = "File deleted",
    };
    mftp_server_msg_write(client_ctx->cmd_fd, &msg);

cleanup:
    free(arg);
}

void mftp_handle_size(command_handler_arg_t* arg) {
    mftp_client_msg_t cmd = arg->cmd;
    mftp_client_ctx_t* client_ctx = arg->client_ctx;

    if (~client_ctx->creds.perms & PERM_READ) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_FORBIDDEN,
            .data = "Permission denied",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    if (strlen(cmd.data) == 0) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_EXPECTED_ARGUMENT,
            .data = "Filename not provided",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    char file_path_full[PATH_MAX] = { 0 };
    sprintf(file_path_full, "%.*s%.*s/", (int)strlen(client_ctx->server_ctx->cfg.root_dir), client_ctx->server_ctx->cfg.root_dir, (int)strlen(client_ctx->cwd), client_ctx->cwd);
    strcat(file_path_full, cmd.data);
    path_normalize(file_path_full);

    struct stat file_stat;
    if (stat(file_path_full, &file_stat) == -1) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_FS_READ_FAILURE,
            .data = "Failed to stat file",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_OK,
        .code = MFTP_CODE_GENERAL_SUCCESS,
        .data = { 0 },
    };
    sprintf(msg.data, "%ld", file_stat.st_size);

    mftp_server_msg_write(client_ctx->cmd_fd, &msg);

cleanup:
    free(arg);
}

// TODO: As for now, ABOR will cause server to complain - there will be reading errors (from file or socket) - no data is leaked, but logs will be dirty with meaningless errors.
void mftp_handle_abor(command_handler_arg_t* arg) {
    mftp_client_ctx_t* client_ctx = arg->client_ctx;

    if (!client_ctx->t_active) {
        mftp_server_msg_t msg = {
            .kind = MFTP_MSG_ERR,
            .code = MFTP_CODE_GENERAL_FAILURE,
            .data = "No transfer in progress",
        };
        mftp_server_msg_write(client_ctx->cmd_fd, &msg);
        goto cleanup;
    }

    client_ctx_cleanup_transfer(client_ctx);

    mftp_server_msg_t msg = {
        .kind = MFTP_MSG_OK,
        .code = MFTP_CODE_TRANSFER_ABORTED,
        .data = "Transfer aborted",
    };
    mftp_server_msg_write(client_ctx->cmd_fd, &msg);

cleanup:
    free(arg);
}

// "extern"ed in handlers.h:

const command_handler_t command_table[] = {
    { MFTP_CMD_NOOP, mftp_handle_noop },
    { MFTP_CMD_QUIT, mftp_handle_quit },
    { MFTP_CMD_FEAT, mftp_handle_feat },
    { MFTP_CMD_USER, mftp_handle_user },
    { MFTP_CMD_PASS, mftp_handle_pass },
    { MFTP_CMD_WAMI, mftp_handle_wami },
    { MFTP_CMD_LIST, mftp_handle_list },
    { MFTP_CMD_RETR, mftp_handle_retr },
    { MFTP_CMD_STOR, mftp_handle_stor },
    { MFTP_CMD_PWDR, mftp_handle_pwdr },
    { MFTP_CMD_CHWD, mftp_handle_chwd },
    { MFTP_CMD_DELE, mftp_handle_dele },
    { MFTP_CMD_SIZE, mftp_handle_size },
    { MFTP_CMD_ABOR, mftp_handle_abor },
};
const size_t command_table_size = sizeof(command_table) / sizeof(command_table[0]);
