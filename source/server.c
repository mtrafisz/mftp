#include "proto/config.h"
#include "proto/server_msg.h"
#include "proto/client_msg.h"
#include "proto/utils.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>

const char* SUPPORTED_COMMANDS[] = {
    "NOOP",
    "LIST",
    "USER",
    "PASS",
    "PWDR",
    "CHWD",
    "ABOR",
    "STOR",
    "RETR",
    "FEAT",
};
#define SUPPORTED_COMMANDS_COUNT (sizeof(SUPPORTED_COMMANDS) / sizeof(char*))

sig_atomic_t abort_serv = false;
void sigint_handler(int sig) {
    abort_serv = true;
    fprintf(stderr, "\nSIGINT detected, shutting down...\n");
}

typedef struct {
    uint16_t allow_anonymous_login : 1;
} ServerConfig;

typedef struct {
    char username[48];
    char password[48];
} UserCreds;

typedef struct {
    int sockfd;
    UserCreds* users;
    size_t user_count;
    ServerConfig cfg;
} ClientConnectionContext;

typedef struct {
    int type;
    int datasockfd;
    int commandsockfd;
    bool transfer_running;
    pthread_t tid;
    FILE* fp;
} TransferContext;

void* stor_handler(void* ctx) {
    TransferContext* tctx = (TransferContext*)ctx;

    char buffer[4096] = { 0 };
    ssize_t bytes_read;

    while ((bytes_read = read(tctx->datasockfd, buffer, sizeof buffer)) > 0 && tctx->transfer_running) {
        fwrite(buffer, bytes_read, 1, tctx->fp);
    }

    // todo: check errno, notify client of any errors (ex. timeout, etc).

    ServerMessage msg = {
        .kind = MSG_AOK,
        .code = ClosingDataChannel,
        .data = "Closing data channel",
    };

    servermsg_write(tctx->commandsockfd, &msg);
    servermsg_reset(&msg);

    shutdown(tctx->datasockfd, SHUT_RDWR);
    close(tctx->datasockfd);

    fflush(tctx->fp);    
    fclose(tctx->fp);   

    tctx->transfer_running = false;
    tctx->datasockfd = -1;

    return NULL; 
}

void* retr_handler(void* ctx) {
    TransferContext* tctx = (TransferContext*)ctx;

    char buffer[4096] = { 0 };
    ssize_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof buffer, tctx->fp)) > 0 && tctx->transfer_running) {
        send(tctx->datasockfd, buffer, bytes_read, 0);
    }

    ServerMessage msg = {
        .kind = MSG_AOK,
        .code = ClosingDataChannel,
        .data = "Closing data channel",
    };

    servermsg_write(tctx->commandsockfd, &msg);
    servermsg_reset(&msg);

    shutdown(tctx->datasockfd, SHUT_RDWR);
    close(tctx->datasockfd);

    fflush(tctx->fp);    
    fclose(tctx->fp);   

    tctx->transfer_running = false;
    tctx->datasockfd = -1;

    return NULL; 
}

void* client_conn_handler(void* ctx) {
    ClientConnectionContext* cctx = (ClientConnectionContext*)ctx;

    /* Initial connection message | authentication, cwd and transfer context set-up */

    bool logged_in = true;  // as anon :3
    UserCreds creds = {
        .username = "anon",
        .password = "",
    };

    char current_dir[256] = ".";

    TransferContext tctx = {
        .tid = 0,
        .datasockfd = -1,
        .commandsockfd = cctx->sockfd,
        .transfer_running = false,
        .fp = NULL,
    };

    ServerMessage msg = {
        .kind = MSG_AOK,
        .code = Ready,
        .data = "Welcome to MFTP server v0.0.420"
    };

    if (cctx->cfg.allow_anonymous_login == false) {
        strcat(msg.data, "\nAnonymous login disabled - login to continue");
        logged_in = false;
        memset(creds.password, 0, 48);
        memset(creds.username, 0, 48);
    }

    servermsg_write(cctx->sockfd, &msg);
    servermsg_reset(&msg);

    /* Message loop */

    while (!abort_serv) {
        /* Check state of transfer thread */

        if (tctx.tid != 0 && tctx.transfer_running == false) {
            pthread_join(tctx.tid, NULL);
            tctx.tid = 0;
            log_info("Data transfer with client %s (%d) ended", creds.username, tctx.commandsockfd);
        }

        /* Receive command */

        ssize_t recv_size;
        char* recv_bytes = read_until(cctx->sockfd, "\r\n", &recv_size, &abort_serv);
        
        if (recv_size == 0) goto close_connection;
        else if (recv_size == -1) continue;

        /* Parse command */

        char *data, *cmd_buff;
        split_by(recv_bytes, ' ', &cmd_buff, &data);
        
        if (!cmd_buff) {
            log_err("recv_bytes is not null, but cmd buff is!");    // This should never happen :)
            free(recv_bytes);
            goto close_connection;
        }

        if (data) { // data is not required for some commands
            strip_ws(&data);
        }
        strip_ws(&cmd_buff);
        to_upper(cmd_buff);

        log_info("Client %s (%d): COMMAND: %s; DATA: %s", creds.username, cctx->sockfd, cmd_buff, data ? data : "NO DATA");

        int cmd = mftp_atoc(cmd_buff);
        if (cmd == INVALID_COMMAND) {
            msg.kind = MSG_ERR;
            msg.code = InvalidCommand,
            sprintf(msg.data, "Unknown command: %s", cmd_buff);

            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);
            goto next_iter;
        }

        /* Process command and send response(s) */

        if (!logged_in && cmd != USER && cmd != PASS) {
            msg.kind = MSG_ERR;
            msg.code = NotLoggedIn;
            strcat(msg.data, "Log in to continue");

            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);
            goto next_iter;
        }

        switch (cmd) {
        case NOOP: {    // just send "Ready" message
            msg.kind = MSG_AOK;
            msg.code = Ready;
            strcat(msg.data, "Ready");

            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);
        } break;
        case LIST: {    // send formated directory listing through DATA channel
            DIR* d = opendir(current_dir);
            if (!d) {
                msg.kind = MSG_ERR;
                msg.code = FsReadFailure;
                strcat(msg.data, "Couldn't open served directory for listing");

                servermsg_write(cctx->sockfd, &msg);
                servermsg_reset(&msg);

                log_err("opendir() FAILED in %s:%d", __FILE__, __LINE__);
                goto next_iter;
            }

            BoundSocket bs = { 0 };
            if (!boundsocket_init_temp(&bs)) {
                msg.kind = MSG_ERR;
                msg.code = DataChannelError;
                strcat(msg.data, "Couldn't open data channel");

                servermsg_write(cctx->sockfd, &msg);
                servermsg_reset(&msg);

                goto next_iter;
            }

            msg.kind = MSG_AOK;
            msg.code = OpeningDataChannel;
            sprintf(msg.data, "[%s %d] Openning DATA channel", inet_ntoa((struct in_addr) { bs.addr } ), bs.port);

            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);

            int data_sockfd = boundsocket_accept(&bs);
            if (data_sockfd == -1) {
                goto close_data_channel_LIST;
            }

            struct dirent* entry;
            char buffer[512] = { 0 };

            while ((entry = readdir(d))) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

                char* type;
                switch (entry->d_type) {
                case DT_DIR: type = "DIRECTORY"; break;
                case DT_REG: type = "FILE"; break;
                default: type = "OTHER"; break;
                }

                strcat(buffer, entry->d_name);     // sprintf doesn't like char[256] for some reason
                sprintf(buffer + strlen(buffer), "\t%s\r\n", type);
                write(data_sockfd, buffer, strlen(buffer));
                memset(buffer, 0, strlen(buffer));
            }

        close_data_channel_LIST:
            msg.kind = MSG_AOK;
            msg.code = ClosingDataChannel;
            strcat(msg.data, "Closing DATA channel");

            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);

            boundsocket_destroy(&bs);
        } break;
        case USER: {    // clear creds and set username
            if (!data) {
                msg.kind = MSG_ERR;
                msg.code = ExpectedArgument;
                strcat(msg.data, "Expected additional argument: username");
                goto send_msg_USER;
            } else if (strlen(data) > 48) {
                msg.kind = MSG_ERR;
                msg.code = InvalidArgument;
                strcat(msg.data, "Invalid username");
                goto send_msg_USER;
            }

            logged_in = false;
            memset(creds.password, 0, 48);
            memset(creds.username, 0, 48);

            strcat(creds.username, data);

            msg.kind = MSG_AOK;
            msg.code = ProvidePassword;
            strcat(msg.data, "Password required");
        send_msg_USER:
            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);
        } break;
        case PASS: {    // finish log-in process
            if (!data) {
                msg.kind = MSG_ERR;
                msg.code = ExpectedArgument;
                strcat(msg.data, "Expected additional argument: password");
                goto send_msg_PASS;
            } else if (strlen(data) > 48) {
                msg.kind = MSG_ERR;
                msg.code = InvalidArgument;
                strcat(msg.data, "Invalid password");
                goto send_msg_PASS;
            } else if (logged_in) {
                msg.kind = MSG_ERR;
                msg.code = InvalidCommand;
                strcat(msg.data, "Already logged in");
                goto send_msg_PASS;
            } else if (strlen(creds.username) == 0) {
                msg.kind = MSG_ERR;
                msg.code = InvalidCommand;
                strcat(msg.data, "Provide username first");
                goto send_msg_PASS;
            }

            memcpy(creds.password, data, strlen(data));

            bool matching_creds_found = false;
            
            for (int i = 0; i < cctx->user_count; ++i) {
                if (strcmp(cctx->users[i].username, creds.username) == 0 &&
                    strcmp(cctx->users[i].password, creds.password) == 0) {
                    matching_creds_found = true;
                }
            }

            if ((cctx->cfg.allow_anonymous_login && strcmp(creds.username, "anon") == 0) || 
                matching_creds_found) {
                msg.kind = MSG_AOK;
                msg.code = LoggedIn;
                sprintf(msg.data, "Logged in as %s", creds.username);

                logged_in = true;
            } else {
                msg.kind = MSG_ERR;
                msg.code = NotLoggedIn;
                strcat(msg.data, "Invalid username or password");

                memset(creds.password, 0, 48);
                memset(creds.username, 0, 48);
            }

        send_msg_PASS:
            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);
        } break;
        case QUIT: {    // send service-closing message and break loop
            msg.kind = MSG_AOK;
            msg.code = ServiceClosing;
            strcat(msg.data, "Closing COMMAND channel");

            servermsg_write(cctx->sockfd, &msg);
            // servermsg_reset(&msg); // not realy needed

            free(recv_bytes);
            goto close_connection;
        } break;
        case PWDR: {    // send relative path of current working directory
            msg.kind = MSG_AOK;
            msg.code = GeneralSuccess;
            strcat(msg.data, current_dir);
            
            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);
        } break;
        case CHWD: {    // change current directory
            if (!data) {
                msg.kind = MSG_ERR;
                msg.code = ExpectedArgument;
                strcat(msg.data, "Expected additional argument: directory name");
                goto send_msg_PASS;
            }

            char temp_cd[256];
            strcpy(temp_cd, current_dir);

            if (!path_cd(temp_cd, data)) {
                msg.kind = MSG_ERR;
                msg.code = Forbidden;
                strcat(msg.data, "Can't exit served directory");
                goto send_msg_CHWD;
            }

            if (opendir(temp_cd) == NULL) {
                msg.kind = MSG_ERR;
                msg.code = FsActionFailure;
                strcat(msg.data, "Invalid path or non-existent directory");
                goto send_msg_CHWD;
            }

            memcpy(current_dir, temp_cd, 256);

            msg.kind = MSG_AOK;
            msg.code = FsActionSuccess;
            sprintf(msg.data, "%s", current_dir);

        send_msg_CHWD:
            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);
        } break;
        case ABOR: {    // if transfer in progress, set tctx.transfer_running to false
            if (!tctx.transfer_running) {
                msg.kind = MSG_ERR;
                msg.code = GeneralFailure;
                strcat(msg.data, "No transfer is in progress");
            } else {
                tctx.transfer_running = false;
                pthread_join(tctx.tid, NULL);

                msg.kind = MSG_AOK;
                msg.code = ClosingDataChannel;
                strcat(msg.data, "Transfer aborted");
            }

            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);
        } break;
        case STOR: {    // set transfer context and run stor_handler() in thread
        // TODO: save file as .temp first - only replace the original, if transfer was successfull.
            if (!data) {
                msg.kind = MSG_ERR;
                msg.code = ExpectedArgument;
                strcat(msg.data, "Expected additional argument: file name");
                goto send_msg_STOR;
            }
            if (strchr(data, '/') != NULL) {
                msg.kind = MSG_ERR;
                msg.code = InvalidArgument;
                strcat(msg.data, "Expected filename, got path");
                goto send_msg_STOR;
            }

            char full_path[512] = { 0 };
            sprintf(full_path, "%s/%s", current_dir, data);

            FILE* outfp = fopen(full_path, "wb");
            if (!outfp) {
                msg.kind = MSG_ERR;
                msg.code = FsWriteFailure;
                strcat(msg.data, "Couldn't open file for writing"); 
                goto send_msg_STOR;
            }

            BoundSocket bs = { 0 };
            if (!boundsocket_init_temp(&bs)) {
                msg.kind = MSG_ERR;
                msg.code = DataChannelError;
                strcat(msg.data, "Couldn't open data channel");

                fclose(outfp);
                goto send_msg_STOR;
            }

            msg.kind = MSG_AOK;
            msg.code = OpeningDataChannel;
            sprintf(msg.data, "[%s %d] Openning DATA channel", inet_ntoa((struct in_addr) { bs.addr } ), bs.port);

            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);

            int data_sockfd = boundsocket_accept(&bs);
            if (data_sockfd == -1) {
                boundsocket_destroy(&bs);
        
                msg.kind = MSG_AOK;
                msg.code = ClosingDataChannel;
                strcat(msg.data, "Closing DATA channel");

                fclose(outfp);
                goto send_msg_STOR;
            }

            tctx.transfer_running = true;
            tctx.datasockfd = data_sockfd;
            tctx.fp = outfp;
            tctx.type = STOR;
            pthread_create(&tctx.tid, NULL, stor_handler, &tctx);

            goto next_iter;

        send_msg_STOR:
            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);
        } break;
        case RETR: {    // set transfer context and run retr_handler() in thread
            if (!data) {
                msg.kind = MSG_ERR;
                msg.code = ExpectedArgument;
                strcat(msg.data, "Expected additional argument: file name");
                goto send_msg_RETR;
            }
            if (strchr(data, '/') != NULL) {
                msg.kind = MSG_ERR;
                msg.code = InvalidArgument;
                strcat(msg.data, "Expected filename, got path");
                goto send_msg_RETR;
            }

            char full_path[512] = { 0 };
            sprintf(full_path, "%s/%s", current_dir, data);

            FILE* outfp = fopen(full_path, "rb");
            if (!outfp) {
                msg.kind = MSG_ERR;
                msg.code = FsWriteFailure;
                strcat(msg.data, "Couldn't open file for writing"); 
                goto send_msg_RETR;
            }

            BoundSocket bs = { 0 };
            if (!boundsocket_init_temp(&bs)) {
                msg.kind = MSG_ERR;
                msg.code = DataChannelError;
                strcat(msg.data, "Couldn't open data channel");

                fclose(outfp);
                goto send_msg_RETR;
            }

            msg.kind = MSG_AOK;
            msg.code = OpeningDataChannel;
            sprintf(msg.data, "[%s %d] Openning DATA channel", inet_ntoa((struct in_addr) { bs.addr } ), bs.port);

            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);

            int datasockfd = boundsocket_accept(&bs);
            if (datasockfd == -1) {
                boundsocket_destroy(&bs);
        
                msg.kind = MSG_AOK;
                msg.code = ClosingDataChannel;
                strcat(msg.data, "Closing DATA channel");

                fclose(outfp);
                goto send_msg_RETR;
            }

            tctx.transfer_running = true;
            tctx.datasockfd = datasockfd;
            tctx.fp = outfp;
            tctx.type = RETR;
            pthread_create(&tctx.tid, NULL, retr_handler, &tctx);

            goto next_iter;

        send_msg_RETR:
            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);
        } break;
        case FEAT: {    // append all supported commands to message data
            msg.kind = MSG_AOK;
            msg.code = GeneralSuccess;
            
            strcat(msg.data, SUPPORTED_COMMANDS[0]);
            for (int i = 1; i < SUPPORTED_COMMANDS_COUNT; ++i) {
                strcat(msg.data, ",");
                strcat(msg.data, SUPPORTED_COMMANDS[i]);
            }

            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);
        } break;
        default: {      // command not implemented
            msg.kind = MSG_ERR;
            msg.code = NotImplemented;
            sprintf(msg.data, "Command recognized but not implemented: %s", cmd_buff);

            servermsg_write(cctx->sockfd, &msg);
            servermsg_reset(&msg);
        }
        }
    
        /* Post-command cleanup */

    next_iter:
        free(recv_bytes);
    }

    /* Connection ended */

close_connection:
    log_info("Client %s (%d) disconnected", creds.username, cctx->sockfd);
    shutdown(cctx->sockfd, SHUT_RDWR);
    close(cctx->sockfd);
    free(ctx);
    return NULL;
}

int main(void) {
    // TEMP // TODO: Read from file / cache somehow / idk?

    UserCreds users[] = {
        {"admin", "admin123"},
    };
    ServerConfig cfg = { 0 };
    cfg.allow_anonymous_login = true;

    // END TEMP

    signal(SIGINT, sigint_handler);

    BoundSocket ssock = { 0 };
    if (!boundsocket_init(&ssock, INADDR_LOOPBACK, MFTP_COMMAND_PORT, 10)) return 1;

    log_info("Server running on %s:%d", inet_ntoa((struct in_addr){ htonl(ssock.addr) }), ssock.port);

    while (!abort_serv) {
        int csockfd = boundsocket_accept(&ssock);
        if (csockfd == -1) {
            switch (errno) {
            case EAGAIN:
            case EINTR:
                continue;
            default:
                perror("accept()");
                continue;
            }
        }

        ClientConnectionContext* ctx = malloc(sizeof(ClientConnectionContext));
        ctx->sockfd = csockfd;
        ctx->users = (UserCreds*)&users;
        ctx->user_count = sizeof(users) / sizeof(UserCreds);
        ctx->cfg = cfg;

        pthread_t client_handle;
        pthread_create(&client_handle, NULL, client_conn_handler, ctx);
        pthread_detach(client_handle);
    }

    boundsocket_destroy(&ssock);
    return 0;
}
